// cpbob_inject.exe - loads cpbob.dll into Minecraft Bedrock (Minecraft.Windows.exe).
// Run by CheesyProxxy.exe when "Sword bob" is switched ON. Same folder as the exe.
// Exit code:  0 = loaded (or was already loaded)   1 = Minecraft is not running   2 = failed
// Build: cl /nologo /O2 /MT /EHsc cpbob_inject.cpp /link /OUT:cpbob_inject.exe advapi32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>

static const wchar_t* kDir  = L"C:\\Users\\Public\\CheesyProxxy";
static const wchar_t* kDll  = L"cpbob.dll";
static const wchar_t* kProc = L"Minecraft.Windows.exe";

// Minecraft is a packaged (UWP) app: it can only read files that "ALL APPLICATION PACKAGES"
// (S-1-15-2-1) may read, so grant that on the shared folder (inherited by bob.cfg) and the DLL.
static bool grantAppPackages(const wchar_t* path, bool folder) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) return false;
    PACL oldAcl = nullptr, newAcl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr; bool ok = false;
    if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &oldAcl, nullptr, &sd) == ERROR_SUCCESS) {
        EXPLICIT_ACCESSW ea; ZeroMemory(&ea, sizeof(ea));
        ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = folder ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = (LPWSTR)sid;
        if (SetEntriesInAclW(1, &ea, oldAcl, &newAcl) == ERROR_SUCCESS) {
            ok = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                       nullptr, nullptr, newAcl, nullptr) == ERROR_SUCCESS;
            LocalFree(newAcl);
        }
    }
    if (sd) LocalFree(sd);
    LocalFree(sid);
    return ok;
}

static DWORD findProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe); DWORD pid = 0;
    for (BOOL b = Process32FirstW(snap, &pe); b; b = Process32NextW(snap, &pe))
        if (lstrcmpiW(pe.szExeFile, kProc) == 0) { pid = pe.th32ProcessID; break; }
    CloseHandle(snap);
    return pid;
}

static bool alreadyLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me; me.dwSize = sizeof(me); bool found = false;
    for (BOOL b = Module32FirstW(snap, &me); b; b = Module32NextW(snap, &me))
        if (lstrcmpiW(me.szModule, kDll) == 0) { found = true; break; }
    CloseHandle(snap);
    return found;
}

int main() {
    // 1) shared folder + permissions (also makes the folder bob.cfg is written to)
    CreateDirectoryW(kDir, nullptr);
    grantAppPackages(kDir, true);

    // 2) copy cpbob.dll from our own folder into the shared folder
    wchar_t self[MAX_PATH]; DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 2;
    wchar_t* slash = wcsrchr(self, L'\\');
    if (!slash) return 2;
    *(slash + 1) = 0;
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    lstrcpyW(src, self); lstrcatW(src, kDll);
    lstrcpyW(dst, kDir); lstrcatW(dst, L"\\"); lstrcatW(dst, kDll);

    DWORD pid = findProcess();
    if (!pid) return 1;
    if (alreadyLoaded(pid)) return 0;                // DLL is live in the game: it only needs bob.cfg

    if (!CopyFileW(src, dst, FALSE) && GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) return 2;
    grantAppPackages(dst, false);

    // 3) classic LoadLibraryW remote thread (does not touch any other module in the game)
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                              PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) return 2;
    int rc = 2;
    SIZE_T bytes = (lstrlenW(dst) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (remote && loadLib && WriteProcessMemory(proc, remote, dst, bytes, nullptr)) {
        HANDLE t = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLib, remote, 0, nullptr);
        if (t) {
            if (WaitForSingleObject(t, 8000) == WAIT_OBJECT_0) {
                DWORD code = 0;
                if (GetExitCodeThread(t, &code) && code != 0) rc = 0;   // low 32 bits of the HMODULE
            }
            CloseHandle(t);
        }
    }
    if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return rc;
}
