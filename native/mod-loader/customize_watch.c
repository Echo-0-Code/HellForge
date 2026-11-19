/* A read-only watch of the armory customize screen's card list, build 25480438: the screen object S (BuildCustomizationList
 * 0x1493770's rcx) holds the list at S+8 (64 entries of 0x18 from S+0x40, its own fields after them), and the screen code
 * reaches it by fixed offsets. To make the list longer every instruction that touches it has to be known, so while a
 * screen is open its list pages are closed and each instruction that touches the list is noted once, with the offset it
 * touched. Off unless HELLFORGE_CUSTOMIZE_WATCH=1; the game runs slower while a guarded screen is open. Included by
 * customize_list.c. */
#define CW_BUILD 0x1493770u    /* BuildCustomizationList(S, weapon) */
#define CW_CLOSE 0x1493100u    /* the screen's tear-down: S+0x8e8cf8 = 0 */
#define CW_LIST 8u             /* the list, from S */
#define CW_LIST_END 0x660u     /* S+0x660: the screen's UI */
#define CW_HITS 2048
#define CW_GUARD_MS 600000u    /* a screen is watched ten minutes at most */
static const uint8_t kCwBuildPin[] = {0x40, 0x53, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00};
static const uint8_t kCwClosePin[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};

typedef void (*CwBuildFn)(uint8_t* screen, const uint32_t* weapon);
typedef void (*CwCloseFn)(uint8_t* screen);
static CwBuildFn g_cwBuild;
static CwCloseFn g_cwClose;
static int g_cwState; /* 0 not yet, 1 hooked, -1 off or failed */
static uintptr_t g_cwGame;
static struct { uint64_t rip; uint32_t offset, last, write; } g_cwHits[CW_HITS]; /* first and highest offset */
static volatile LONG g_cwHitCount;
static LONG g_cwNoted;
static volatile uintptr_t g_cwFrom, g_cwTo, g_cwPageFrom, g_cwPageTo; /* the list, its pages; 0 when none is watched */
static ULONGLONG g_cwSince;
static volatile DWORD g_cwOwner;
static uintptr_t g_cwOpen[4];
static int g_cwOpened;
static struct { uint32_t weapon, items, entries; } g_cwBuilt[64];
static volatile LONG g_cwBuiltCount;
static LONG g_cwBuiltNoted;
static volatile LONG g_cwReachDue; /* a screen closed: note each instruction's reach */

/* One line per instruction: the first offset it touched; a higher one later is noted again (a loop's reach). */
static void CwRecord(uintptr_t address, uint64_t rip, uint32_t write)
{
    uint32_t offset = (uint32_t)(address - g_cwFrom);
    LONG count = g_cwHitCount;
    for (LONG i = 0; i < count; i++)
        if (g_cwHits[i].rip == rip)
        {
            if (offset > g_cwHits[i].last) g_cwHits[i].last = offset;
            g_cwHits[i].write |= write;
            return;
        }
    if (count >= CW_HITS) return;
    g_cwHits[count].rip = rip;
    g_cwHits[count].offset = g_cwHits[count].last = offset;
    g_cwHits[count].write = write;
    InterlockedIncrement(&g_cwHitCount);
}

/* The instruction let through has run: its pages close again. */
static void CwClose(void)
{
    DWORD protect;
    if (g_cwPageFrom)
        for (int i = 0; i < g_cwOpened; i++) VirtualProtect((void*)g_cwOpen[i], 0x1000, PAGE_NOACCESS, &protect);
    g_cwOpened = 0;
    InterlockedExchange((volatile LONG*)&g_cwOwner, 0);
}

static LONG CALLBACK CwGuard(EXCEPTION_POINTERS* e)
{
    DWORD thread = GetCurrentThreadId(), protect;
    if (SafeReadOwns(e->ContextRecord->Rip)) return EXCEPTION_CONTINUE_SEARCH;
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && g_cwOwner == thread)
    {
        CwClose();
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t address = (uintptr_t)e->ExceptionRecord->ExceptionInformation[1];
    if (address < g_cwPageFrom || address >= g_cwPageTo) return EXCEPTION_CONTINUE_SEARCH;
    if (g_cwOwner != thread)
        while (InterlockedCompareExchange((volatile LONG*)&g_cwOwner, (LONG)thread, 0) != 0) YieldProcessor();
    if (address >= g_cwFrom && address < g_cwTo) CwRecord(address, e->ContextRecord->Rip, (uint32_t)e->ExceptionRecord->ExceptionInformation[0]);
    uintptr_t page = address & ~(uintptr_t)0xFFF;
    if (g_cwOpened < 4) g_cwOpen[g_cwOpened++] = page;
    VirtualProtect((void*)page, 0x1000, PAGE_READWRITE, &protect);
    e->ContextRecord->EFlags |= 0x100; /* the trap flag: back here after this instruction */
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* Opens the watched pages for good (the screen closes, or its time is up). Called on the game thread. */
static void CwUnguard(void)
{
    if (!g_cwPageFrom) return;
    while (InterlockedCompareExchange((volatile LONG*)&g_cwOwner, (LONG)GetCurrentThreadId(), 0) != 0) YieldProcessor();
    DWORD protect;
    VirtualProtect((void*)g_cwPageFrom, g_cwPageTo - g_cwPageFrom, PAGE_READWRITE, &protect);
    g_cwPageFrom = g_cwPageTo = g_cwFrom = g_cwTo = 0;
    g_cwOpened = 0;
    InterlockedExchange((volatile LONG*)&g_cwOwner, 0);
}

static void CwGuardScreen(uint8_t* screen)
{
    uintptr_t from = (uintptr_t)screen + CW_LIST, to = (uintptr_t)screen + CW_LIST_END;
    if (g_cwFrom == from) return;
    CwUnguard();
    g_cwFrom = from;
    g_cwTo = to;
    g_cwPageTo = (to + 0xFFF) & ~(uintptr_t)0xFFF;
    g_cwPageFrom = from & ~(uintptr_t)0xFFF;
    g_cwSince = GetTickCount64();
    DWORD protect;
    if (!VirtualProtect((void*)g_cwPageFrom, g_cwPageTo - g_cwPageFrom, PAGE_NOACCESS, &protect)) g_cwPageFrom = g_cwPageTo = g_cwFrom = g_cwTo = 0;
}

static void CwBuildDetour(uint8_t* screen, const uint32_t* weapon)
{
    CwUnguard(); /* the build itself runs open: its writes are reviewed (registry.c) */
    g_cwBuild(screen, weapon);
    LONG n = g_cwBuiltCount;
    if (n < 64)
    {
        uint32_t entries = 0;
        for (uint32_t i = 0; i < 64; i++) if (*(uint64_t*)(screen + 0x40 + 0x18u * i)) entries++;
        g_cwBuilt[n].weapon = weapon ? *weapon : 0;
        g_cwBuilt[n].items = *(uint32_t*)(screen + 0x3C);
        g_cwBuilt[n].entries = entries;
        InterlockedIncrement(&g_cwBuiltCount);
    }
    CwGuardScreen(screen);
}

static void CwCloseDetour(uint8_t* screen)
{
    if (g_cwFrom == (uintptr_t)screen + CW_LIST) { CwUnguard(); g_cwReachDue = 1; }
    g_cwClose(screen);
}

static int CwWanted(void)
{
    wchar_t value[8];
    DWORD length = GetEnvironmentVariableW(L"HELLFORGE_CUSTOMIZE_WATCH", value, 8);
    return length > 0 && length < 8 && value[0] == L'1';
}

static void CustomizeWatchInstall(void)
{
    if (g_cwState) return;
    g_cwState = -1;
    if (!CwWanted() || strcmp(g_build, "25480438")) return;
    uintptr_t game = ApiModuleBase("game.dll", NULL);
    if (!game || !PinMatches(game + CW_BUILD, kCwBuildPin, sizeof kCwBuildPin) || !PinMatches(game + CW_CLOSE, kCwClosePin, sizeof kCwClosePin))
    {
        Note("customize watch: the screen code does not match the reviewed build; not watched");
        return;
    }
    g_cwGame = game;
    AddVectoredExceptionHandler(1, CwGuard);
    if (LoaderHook("customize watch: the list build", game + CW_BUILD, (void*)CwBuildDetour, (void**)&g_cwBuild) ||
        LoaderHook("customize watch: the screen's tear-down", game + CW_CLOSE, (void*)CwCloseDetour, (void**)&g_cwClose))
    {
        Note("customize watch: could not hook the screen; not watched");
        return;
    }
    g_cwState = 1;
    Note("customize watch: on (HELLFORGE_CUSTOMIZE_WATCH=1); each instruction that touches a customize screen's list is noted once");
}

static void CwNoteReach(void)
{
    for (LONG i = 0; i < g_cwHitCount && i < CW_HITS; i++)
        if (g_cwHits[i].last != g_cwHits[i].offset)
            Note("customize watch: game.dll+0x%llx reached list+0x%03x..0x%03x%s", (unsigned long long)(g_cwHits[i].rip - g_cwGame),
                g_cwHits[i].offset, g_cwHits[i].last, g_cwHits[i].write ? " (writes)" : "");
}

static void CwNoteHit(LONG i)
{
    uint64_t rip = g_cwHits[i].rip;
    const char* how = g_cwHits[i].write ? "written" : "read";
    if (rip >= g_cwGame && rip < g_cwGame + 0x4000000u)
        Note("customize watch: list+0x%03x %s by game.dll+0x%llx", g_cwHits[i].offset, how, (unsigned long long)(rip - g_cwGame));
    else
        Note("customize watch: list+0x%03x %s by 0x%llx (outside game.dll)", g_cwHits[i].offset, how, (unsigned long long)rip);
}

/* New hits and builds, in modloader.log; the guard ends after CW_GUARD_MS. Game thread. */
static void CustomizeWatchReport(void)
{
    if (g_cwState != 1) return;
    if (g_cwPageFrom && GetTickCount64() - g_cwSince > CW_GUARD_MS)
    {
        CwUnguard();
        Note("customize watch: ten minutes on one screen; this screen is no longer watched");
        CwNoteReach();
    }
    for (LONG n = g_cwBuiltCount; g_cwBuiltNoted < n; g_cwBuiltNoted++)
        Note("customize watch: built the list of weapon %u: %u items, %u entries of 64", g_cwBuilt[g_cwBuiltNoted].weapon,
            g_cwBuilt[g_cwBuiltNoted].items, g_cwBuilt[g_cwBuiltNoted].entries);
    for (LONG n = g_cwHitCount; g_cwNoted < n && g_cwNoted < CW_HITS; g_cwNoted++) CwNoteHit(g_cwNoted);
    if (InterlockedExchange(&g_cwReachDue, 0)) CwNoteReach();
}
