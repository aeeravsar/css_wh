#include <iostream>
#include <sys/uio.h>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <vector>

using namespace std;

struct MemoryRegion {
    uintptr_t start;
    uintptr_t end;
    string permissions;
    string name;
};

bool readMemory(pid_t pid, uintptr_t address, void* buffer, size_t size) {
    struct iovec local_iov = {buffer, size};
    struct iovec remote_iov = {reinterpret_cast<void*>(address), size};

    ssize_t result = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
    return result == static_cast<ssize_t>(size);
}

bool writeMemory(pid_t pid, uintptr_t address, const void* buffer, size_t size) {
    struct iovec local_iov = {const_cast<void*>(buffer), size};
    struct iovec remote_iov = {reinterpret_cast<void*>(address), size};

    ssize_t result = process_vm_writev(pid, &local_iov, 1, &remote_iov, 1, 0);
    return result == static_cast<ssize_t>(size);
}

pid_t findProcessByName(const string& name) {
    DIR* dir = opendir("/proc");
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        string dirName = entry->d_name;
        if (!all_of(dirName.begin(), dirName.end(), ::isdigit)) {
            continue;
        }

        string cmdlinePath = "/proc/" + dirName + "/cmdline";
        ifstream cmdlineFile(cmdlinePath);
        if (!cmdlineFile.is_open()) {
            continue;
        }

        string cmdline;
        getline(cmdlineFile, cmdline, '\0');
        cmdlineFile.close();

        if (cmdline.find(name) != string::npos) {
            closedir(dir);
            return stoi(dirName);
        }
    }

    closedir(dir);
    return 0;
}

bool attachToProcess(pid_t pid) {
    return true;
}

void detachFromProcess(pid_t pid) {
}

vector<MemoryRegion> getWritableRegions(pid_t pid) {
    vector<MemoryRegion> regions;
    string mapsPath = "/proc/" + to_string(pid) + "/maps";
    ifstream mapsFile(mapsPath);

    if (!mapsFile.is_open()) {
        return regions;
    }

    string line;
    while (getline(mapsFile, line)) {
        MemoryRegion region;
        char perms[5];
        char path[256] = {0};

        sscanf(line.c_str(), "%lx-%lx %4s %*s %*s %*s %255[^\n]",
               &region.start, &region.end, perms, path);

        region.permissions = perms;
        region.name = path;

        if (region.permissions.find("rw") != string::npos &&
            (region.name.find("client.so") != string::npos ||
             region.name.find("engine.so") != string::npos ||
             region.name.find("materialsystem.so") != string::npos ||
             region.name.find("studiorender.so") != string::npos ||
             (region.permissions == "rw-p" && region.name.empty()))) {

            if (region.end - region.start < 10 * 1024 * 1024) {
                regions.push_back(region);
            }
        }
    }

    return regions;
}

uintptr_t findRDrawOtherModelsAddress(pid_t pid) {
    cout << "Finding r_drawothermodels address..." << endl;
    cout << "Set r_drawothermodels to 2 in console, then press Enter...";
    cin.get();

    vector<MemoryRegion> regions = getWritableRegions(pid);
    vector<uintptr_t> candidates;

    for (const auto& region : regions) {
        for (uintptr_t addr = region.start; addr < region.end; addr += 4) {
            int value;
            if (readMemory(pid, addr, &value, sizeof(value))) {
                if (value == 2) {
                    candidates.push_back(addr);
                }
            }
        }
    }

    cout << "Found " << candidates.size() << " addresses with value 2" << endl;

    cout << "Set r_drawothermodels to 1 in console, then press Enter...";
    cin.get();

    vector<uintptr_t> filtered;
    for (uintptr_t addr : candidates) {
        int value;
        if (readMemory(pid, addr, &value, sizeof(value))) {
            if (value == 1) {
                filtered.push_back(addr);
            }
        }
    }

    cout << "Filtered to " << filtered.size() << " addresses" << endl;

    cout << "Set r_drawothermodels to 0 in console, then press Enter...";
    cin.get();

    for (uintptr_t addr : filtered) {
        int value;
        if (readMemory(pid, addr, &value, sizeof(value))) {
            if (value == 0) {
                cout << "Found r_drawothermodels at: 0x" << hex << addr << endl;

                int test = 1;
                writeMemory(pid, addr, &test, sizeof(test));
                cout << "Address confirmed!" << endl;
                return addr;
            }
        }
    }

    cout << "Could not find r_drawothermodels address!" << endl;
    return 0;
}

int main() {
    cout << "Wallhack for Counter Strike: Source on Linux" << endl;
    cout << "=============================================" << endl << endl;
    cout << "Instructions:" << endl;
    cout << "1. Start Counter-Strike Source" << endl;
    cout << "2. Create a local game (offline with bots)" << endl;
    cout << "3. Enable cheats with: sv_cheats 1" << endl;
    cout << "4. Follow the prompts to set r_drawothermodels values" << endl;
    cout << "5. Once the memory address is found, you can join any server" << endl << endl;
    cout << "Waiting for Counter-Strike Source..." << endl;

    pid_t pid = 0;
    while (pid == 0) {
        pid = findProcessByName("cstrike_linux64");
        if (pid == 0) {
            cout << "Game not found, retrying in 1 second..." << endl;
            sleep(1);
        }
    }

    cout << "Found game process! PID: " << pid << endl;

    cout << "Ready to scan memory!" << endl;

    uintptr_t address = findRDrawOtherModelsAddress(pid);
    if (address == 0) {
        detachFromProcess(pid);
        return 1;
    }

    cout << "\nReady! Using address: 0x" << hex << address << endl << endl;

    while (true) {
        int currentValue;
        if (!readMemory(pid, address, &currentValue, sizeof(currentValue))) {
            cout << "Failed to read memory. Game might have closed." << endl;
            break;
        }

        cout << "Current r_drawothermodels value: " << currentValue;
        if (currentValue == 1) cout << " (normal)";
        else if (currentValue == 2) cout << " (wireframe)";
        else if (currentValue == 0) cout << " (invisible)";
        cout << endl;

        cout << "Commands: t=toggle, 0/1/2=set value, q=quit: ";

        string input;
        cin >> input;

        if (input == "q") {
            cout << "Exiting..." << endl;
            detachFromProcess(pid);
            break;
        } else if (input == "t") {
            int newValue = (currentValue == 2) ? 1 : 2;
            if (writeMemory(pid, address, &newValue, sizeof(newValue))) {
                cout << "Toggled to " << newValue << endl;
            } else {
                cout << "Failed to write memory" << endl;
            }
        } else if (input == "0" || input == "1" || input == "2") {
            int newValue = stoi(input);
            if (writeMemory(pid, address, &newValue, sizeof(newValue))) {
                cout << "Set to " << newValue << endl;
            } else {
                cout << "Failed to write memory" << endl;
            }
        } else {
            cout << "Invalid command. Use t, 0/1/2, or q" << endl;
        }

        cout << endl;
    }

    return 0;
}