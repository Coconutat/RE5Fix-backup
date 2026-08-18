// MemoryPatternScanner - diagnostic tool for RE5Fix adaptation.
// Scans the runtime memory of a running RE5 process for the RE5Fix signatures.
// This does NOT install or inject anything into the game.
//
// Usage:
//   MemoryPatternScanner.exe --launch <path-to-re5dx9.exe>
//   MemoryPatternScanner.exe --pid <process-id>
//
// If launched, the process is terminated after scanning.

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cwctype>
#include <cstdlib>
#include <cstring>
#include <algorithm>

struct Pattern
{
    const char* name;
    const char* sig;
};

// Signatures from RE5Fix v1.0.4+ (old source).
static const Pattern g_patterns[] = {
    { "UI1",       "8B ? E0 4E 00 00 ? ? ? ? ? ? ? ? ? 83 ? ? ?" },
    { "UI2",       "8B 81 ? ? ? ? 83 EC ? 83 F8" },
    { "ResLimit",  "8B 0D ? ? ? ? 8A 41" },
    { "FPSCap",    "F3 0F ? ? ? 0F 28 ? F3 0F ? ? F3 0F ? ? ? ? D9 54" },
    { "CrashFix",  "00 00 10 3F 00 60 EA 46 00 A0 0C 47 AC" },
    { "MovieFix",  "F3 0F ? ? F3 0F ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? 75" },
    { "ShadowQ",   "83 C0 ? 56 8B F1 83 E0" },
    { "ColourFilter", "0F 87 ? ? ? ? FF 24 ? ? ? ? ? D9 05 ? ? ? ? 51" },
    { "FOV1",      "F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? 8B 55 ? F3 0F ? ? F3 0F ? ? F3 0F" },
    { "FOV2",      "F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? E9" },
    { "FOV3",      "D9 41 ? 8B 4D ? D9 19" },
    { "FOV4",      "D9 5F ? D9 40 ? D9 1A" },
    { "FOV5",      "D9 5F ? D9 42 ? D9 18" },
};

static std::vector<int> ParseSignature(const char* sig)
{
    std::vector<int> bytes;
    const char* p = sig;
    while (*p)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?')
        {
            bytes.push_back(-1);
            ++p;
            if (*p == '?') ++p;
        }
        else
        {
            char* end = nullptr;
            long v = strtol(p, &end, 16);
            if (end == p) break;
            bytes.push_back(static_cast<int>(v));
            p = end;
        }
    }
    return bytes;
}

static bool ScanBuffer(const std::vector<int>& pat, const uint8_t* data, size_t len, size_t& offset)
{
    if (pat.empty() || len < pat.size()) return false;
    for (size_t i = 0; i + pat.size() <= len; ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < pat.size(); ++j)
        {
            if (pat[j] != -1 && data[i + j] != static_cast<uint8_t>(pat[j]))
            {
                ok = false;
                break;
            }
        }
        if (ok)
        {
            offset = i;
            return true;
        }
    }
    return false;
}

static bool ScanProcess(DWORD pid)
{
    uintptr_t base = 0;
    size_t size = 0;
    bool found = false;

    // Try Toolhelp first; Enigma may deny it, so fall back to fixed RE5 image base.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 me;
        me.dwSize = sizeof(me);

        if (Module32First(snap, &me))
        {
            do
            {
                if (base == 0)
                {
                    // First module is the main executable.
                    base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                    size = me.modBaseSize;
                    found = true;
                    std::wcout << L"Module: " << me.szModule << L" base=0x" << std::hex << base
                               << L" size=0x" << std::hex << size << std::dec << std::endl;
                }
            } while (!found && Module32Next(snap, &me));
        }
        CloseHandle(snap);
    }
    else
    {
        std::wcerr << L"CreateToolhelp32Snapshot failed (error=" << GetLastError()
                   << L"), falling back to fixed RE5 image base." << std::endl;
    }

    if (!found)
    {
        // re5dx9.exe is non-relocatable; ImageBase = 0x400000, SizeOfImage = 0x1929000.
        base = 0x400000;
        size = 0x1929000;
        found = true;
        std::wcout << L"Using fixed RE5 image base=0x" << std::hex << base
                   << L" size=0x" << std::hex << size << std::dec << std::endl;
    }

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return false;
    }

    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    size_t maxPatternLen = 0;
    for (const auto& p : g_patterns)
    {
        size_t len = ParseSignature(p.sig).size();
        if (len > maxPatternLen) maxPatternLen = len;
    }

    // Read the whole image in chunks, keeping overlap for patterns that cross chunks.
    std::vector<uint8_t> whole;
    whole.reserve(size);
    SIZE_T totalRead = 0;
    while (totalRead < size)
    {
        SIZE_T toRead = std::min<size_t>(chunkSize, size - totalRead);
        SIZE_T done = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + totalRead), chunk.data(), toRead, &done))
        {
            std::cerr << "ReadProcessMemory failed at offset 0x" << std::hex << totalRead
                      << " error=" << std::dec << GetLastError() << std::endl;
            break;
        }
        whole.insert(whole.end(), chunk.begin(), chunk.begin() + done);
        totalRead += done;
    }

    CloseHandle(proc);

    bool allFound = true;
    for (const auto& p : g_patterns)
    {
        auto pat = ParseSignature(p.sig);
        size_t off = 0;
        bool ok = ScanBuffer(pat, whole.data(), whole.size(), off);
        std::cout << p.name << " : " << (ok ? "FOUND" : "NOT FOUND");
        if (ok)
        {
            std::cout << " at RVA 0x" << std::hex << off << std::dec;
        }
        std::cout << std::endl;
        if (!ok) allFound = false;
    }
    return allFound;
}

static void PrintHex(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        printf("%02X ", data[i]);
        if ((i & 15) == 15) printf("\n");
    }
    printf("\n");
}

static int FindAll(DWORD pid, const std::string& sig)
{
    const uintptr_t base = 0x400000;
    const size_t size = 0x1929000;

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return 1;
    }

    auto pat = ParseSignature(sig.c_str());
    if (pat.empty())
    {
        std::cerr << "Empty pattern." << std::endl;
        CloseHandle(proc);
        return 1;
    }

    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    std::vector<uint8_t> whole;
    whole.reserve(size);
    SIZE_T totalRead = 0;
    while (totalRead < size)
    {
        SIZE_T toRead = std::min<size_t>(chunkSize, size - totalRead);
        SIZE_T done = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + totalRead), chunk.data(), toRead, &done))
        {
            std::cerr << "ReadProcessMemory failed at offset 0x" << std::hex << totalRead
                      << " error=" << std::dec << GetLastError() << std::endl;
            break;
        }
        whole.insert(whole.end(), chunk.begin(), chunk.begin() + done);
        totalRead += done;
    }
    CloseHandle(proc);

    int count = 0;
    const size_t ctx = 24;
    for (size_t i = 0; i + pat.size() <= whole.size(); ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < pat.size(); ++j)
        {
            if (pat[j] != -1 && whole[i + j] != static_cast<uint8_t>(pat[j]))
            {
                ok = false;
                break;
            }
        }
        if (ok)
        {
            printf("Match %d at RVA 0x%08X\n", ++count, static_cast<unsigned>(i));
            size_t start = i > ctx ? i - ctx : 0;
            size_t len = std::min<size_t>(whole.size() - start, ctx + pat.size() + ctx);
            PrintHex(whole.data() + start, len);
            if (count >= 20) break;
        }
    }
    printf("Total matches shown: %d\n", count);
    return 0;
}

static int FuzzyFind(DWORD pid, const std::string& sig, int maxMismatch)
{
    const uintptr_t base = 0x400000;
    const size_t size = 0x1929000;

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return 1;
    }

    auto pat = ParseSignature(sig.c_str());
    if (pat.empty())
    {
        std::cerr << "Empty pattern." << std::endl;
        CloseHandle(proc);
        return 1;
    }

    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    std::vector<uint8_t> whole;
    whole.reserve(size);
    SIZE_T totalRead = 0;
    while (totalRead < size)
    {
        SIZE_T toRead = std::min<size_t>(chunkSize, size - totalRead);
        SIZE_T done = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + totalRead), chunk.data(), toRead, &done))
        {
            std::cerr << "ReadProcessMemory failed at offset 0x" << std::hex << totalRead
                      << " error=" << std::dec << GetLastError() << std::endl;
            break;
        }
        whole.insert(whole.end(), chunk.begin(), chunk.begin() + done);
        totalRead += done;
    }
    CloseHandle(proc);

    int count = 0;
    const size_t ctx = 16;
    for (size_t i = 0; i + pat.size() <= whole.size(); ++i)
    {
        int mismatches = 0;
        for (size_t j = 0; j < pat.size(); ++j)
        {
            if (pat[j] != -1 && whole[i + j] != static_cast<uint8_t>(pat[j]))
            {
                if (++mismatches > maxMismatch) break;
            }
        }
        if (mismatches <= maxMismatch)
        {
            printf("Fuzzy match %d at RVA 0x%08X (mismatches=%d)\n", ++count, static_cast<unsigned>(i), mismatches);
            size_t start = i > ctx ? i - ctx : 0;
            size_t len = std::min<size_t>(whole.size() - start, ctx + pat.size() + ctx);
            PrintHex(whole.data() + start, len);
            if (count >= 30) break;
        }
    }
    printf("Total fuzzy matches shown: %d\n", count);
    return 0;
}

static int Peek(DWORD pid, uintptr_t rva, size_t len)
{
    const uintptr_t base = 0x400000;
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return 1;
    }

    std::vector<uint8_t> buf(len);
    SIZE_T done = 0;
    if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + rva), buf.data(), len, &done))
    {
        std::cerr << "ReadProcessMemory failed at RVA 0x" << std::hex << rva
                  << " error=" << std::dec << GetLastError() << std::endl;
        CloseHandle(proc);
        return 1;
    }
    CloseHandle(proc);

    printf("Dump RVA 0x%08X (%zu bytes)\n", static_cast<unsigned>(rva), done);
    PrintHex(buf.data(), done);
    return 0;
}

static int FindAllProcessMemory(DWORD pid, const std::string& sig)
{
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return 1;
    }

    auto pat = ParseSignature(sig.c_str());
    if (pat.empty())
    {
        std::cerr << "Empty pattern." << std::endl;
        CloseHandle(proc);
        return 1;
    }

    int count = 0;
    uintptr_t addr = 0x10000;
    const uintptr_t maxAddr = 0x7FFEFFFF;
    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> chunk(chunkSize);

    while (addr < maxAddr)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & 0xFF) != PAGE_NOACCESS &&
            (mbi.Protect & 0xFF) != PAGE_GUARD)
        {
            SIZE_T regionSize = mbi.RegionSize;
            SIZE_T readTotal = 0;
            while (readTotal < regionSize)
            {
                SIZE_T toRead = std::min<size_t>(chunkSize, regionSize - readTotal);
                SIZE_T done = 0;
                uintptr_t regionAddr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + readTotal;
                if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(regionAddr), chunk.data(), toRead, &done))
                {
                    for (size_t i = 0; i + pat.size() <= done; ++i)
                    {
                        bool ok = true;
                        for (size_t j = 0; j < pat.size(); ++j)
                        {
                            if (pat[j] != -1 && chunk[i + j] != static_cast<uint8_t>(pat[j]))
                            {
                                ok = false;
                                break;
                            }
                        }
                        if (ok)
                        {
                            printf("Match %d at VA 0x%08X (RVA 0x%08X)\n", ++count,
                                   static_cast<unsigned>(regionAddr + i),
                                   static_cast<unsigned>((regionAddr + i) - 0x400000));
                            if (count >= 20) break;
                        }
                    }
                }
                else
                {
                    break;
                }
                readTotal += done;
                if (count >= 20) break;
            }
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (count >= 20) break;
    }

    CloseHandle(proc);
    printf("Total matches found: %d\n", count);
    return 0;
}

static int DumpImage(DWORD pid, const std::wstring& path)
{
    const uintptr_t base = 0x400000;
    const size_t size = 0x1929000;

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc)
    {
        std::cerr << "OpenProcess failed, error=" << GetLastError() << std::endl;
        return 1;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        std::cerr << "CreateFile failed, error=" << GetLastError() << std::endl;
        CloseHandle(proc);
        return 1;
    }

    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    SIZE_T totalRead = 0;
    DWORD written = 0;
    while (totalRead < size)
    {
        SIZE_T toRead = std::min<size_t>(chunkSize, size - totalRead);
        SIZE_T done = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + totalRead), chunk.data(), toRead, &done))
        {
            std::cerr << "ReadProcessMemory failed at offset 0x" << std::hex << totalRead
                      << " error=" << std::dec << GetLastError() << std::endl;
            break;
        }
        WriteFile(file, chunk.data(), static_cast<DWORD>(done), &written, nullptr);
        totalRead += done;
    }

    CloseHandle(file);
    CloseHandle(proc);
    std::cout << "Dumped " << totalRead << " bytes to ";
    std::wcout << path << std::endl;
    return 0;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: MemoryPatternScanner.exe --launch <path> | --pid <pid> | --find <hexpattern> --pid <pid> | --peek <pid> <rva> <len> | --dump <pid> <file>" << std::endl;
        return 1;
    }

    std::wstring mode = argv[1];
    if (mode == L"--launch" && argc >= 3)
    {
        std::wstring path = argv[2];
        std::wcout << L"Launching " << path << L" ..." << std::endl;

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::wstring cmdline = L"\"" + path + L"\"";
        if (!CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            std::cerr << "CreateProcess failed, error=" << GetLastError() << std::endl;
            return 1;
        }

        // Give Enigma's unpacker time to decrypt the original code.
        Sleep(12000);
        ScanProcess(pi.dwProcessId);

        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else if (mode == L"--findall" && argc >= 5 && std::wstring(argv[3]) == L"--pid")
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[4]));
        std::string sig(argv[2], argv[2] + wcslen(argv[2]));
        std::cout << "Searching entire process memory for: " << sig << std::endl;
        return FindAllProcessMemory(pid, sig);
    }
    else if (mode == L"--fuzzy" && argc >= 6 && std::wstring(argv[3]) == L"--pid")
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[4]));
        std::string sig(argv[2], argv[2] + wcslen(argv[2]));
        int maxMismatch = _wtoi(argv[5]);
        std::cout << "Fuzzy searching for: " << sig << " maxMismatch=" << maxMismatch << std::endl;
        return FuzzyFind(pid, sig, maxMismatch);
    }
    else if (mode == L"--find" && argc >= 5 && std::wstring(argv[3]) == L"--pid")
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[4]));
        std::string sig(argv[2], argv[2] + wcslen(argv[2]));
        std::cout << "Searching for: " << sig << std::endl;
        return FindAll(pid, sig);
    }
    else if (mode == L"--dump" && argc >= 4)
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[2]));
        return DumpImage(pid, argv[3]);
    }
    else if (mode == L"--peek" && argc >= 5)
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[2]));
        uintptr_t rva = _wcstoui64(argv[3], nullptr, 16);
        size_t len = static_cast<size_t>(_wtoi(argv[4]));
        return Peek(pid, rva, len);
    }
    else if (mode == L"--pid" && argc >= 3)
    {
        DWORD pid = static_cast<DWORD>(_wtoi(argv[2]));
        std::wcout << L"Attaching to PID " << pid << L" ..." << std::endl;
        ScanProcess(pid);
    }
    else
    {
        std::cerr << "Usage: MemoryPatternScanner.exe --launch <path> | --pid <pid>" << std::endl;
        return 1;
    }

    return 0;
}
