#include <iostream>
#include <sys/uio.h>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include <vector>
#include <X11/Xlib.h>
#include <X11/keysym.h>

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


vector<MemoryRegion> getAllRegions(pid_t pid) {
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
        regions.push_back(region);
    }

    return regions;
}


struct ConVar {
    uintptr_t vtable;       // +0
    uintptr_t next;         // +8  - pointer to next ConVar in linked list
    uintptr_t name;         // +16 - pointer to name string
    uintptr_t helpString;   // +24 - pointer to help string
    int flags;              // +32 - ConVar flags
    uintptr_t parent;       // +40 - parent ConVar
    uintptr_t defaultValue; // +48 - default value string
    uintptr_t stringValue;  // +56 - current string value
    int stringLength;       // +64 - string length
    float floatValue;       // +68 - float value
    int intValue;           // +72 - int value (this is what we want!)
    bool hasMin;            // +76 - has minimum value
    float minValue;         // +80 - minimum value
    bool hasMax;            // +84 - has maximum value
    float maxValue;         // +88 - maximum value
    // ... more fields
};



uintptr_t findRDrawOtherModelsAddress(pid_t pid) {
    vector<MemoryRegion> regions = getAllRegions(pid);

    // Find game .so regions to identify which anonymous regions belong to the game
    bool foundGameSo = false;
    uintptr_t lastGameSoEnd = 0;

    for (size_t i = 0; i < regions.size(); i++) {
        const auto& region = regions[i];

        // Check if this is a game .so file
        bool isGameSo = (region.name.find("Counter-Strike Source") != string::npos ||
                        region.name.find("cstrike") != string::npos) &&
                       (region.name.find("client.so") != string::npos ||
                        region.name.find("engine.so") != string::npos ||
                        region.name.find("materialsystem.so") != string::npos ||
                        region.name.find("studiorender.so") != string::npos);

        if (isGameSo) {
            foundGameSo = true;
            lastGameSoEnd = region.end;
        }

        // Search anonymous regions that come after game .so files (their heap)
        if (foundGameSo && region.name.empty() && region.permissions == "rw-p") {
            // Make sure this anonymous region is close to a game .so (within 100MB)
            if (region.start - lastGameSoEnd > 100 * 1024 * 1024) continue;

            // Skip huge regions
            if (region.end - region.start > 10 * 1024 * 1024) continue;

            for (uintptr_t addr = region.start; addr < region.end; addr += 8) {
                ConVar convar;
                if (readMemory(pid, addr, &convar, sizeof(convar))) {
                    // Check if this looks like a ConVar structure
                    if (convar.vtable > 0x400000 && convar.vtable < 0x800000000000 &&
                        convar.name > 0x400000 && convar.name < 0x800000000000) {

                        char nameBuffer[32];
                        if (readMemory(pid, convar.name, nameBuffer, sizeof(nameBuffer))) {
                            nameBuffer[31] = '\0';
                            string name = nameBuffer;

                            if (name == "r_drawothermodels") {
                                // The real value is at offset +80 from the ConVar structure
                                uintptr_t realValueAddr = addr + 80;

                                int currentValue;
                                if (readMemory(pid, realValueAddr, &currentValue, sizeof(currentValue))) {
                                    if (currentValue >= 0 && currentValue <= 2) {
                                        int testVal = (currentValue == 2) ? 1 : 2;
                                        if (writeMemory(pid, realValueAddr, &testVal, sizeof(testVal))) {
                                            int readBack;
                                            if (readMemory(pid, realValueAddr, &readBack, sizeof(readBack))) {
                                                if (readBack == testVal) {
                                                    writeMemory(pid, realValueAddr, &currentValue, sizeof(currentValue)); // Restore
                                                    return realValueAddr;
                                                }
                                            }
                                        }
                                    }
                                }
                                return 0;
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}



int main() {
    cout << "CS:S Wallhack" << endl;
    cout << "Waiting for game..." << endl;

    pid_t pid = 0;
    while (pid == 0) {
        pid = findProcessByName("cstrike_linux64");
        if (pid == 0) {
            sleep(1);
        }
    }

    cout << "Found! PID: " << pid << endl;

    uintptr_t address = findRDrawOtherModelsAddress(pid);
    if (address == 0) {
        cout << "Failed to find r_drawothermodels!" << endl;
        return 1;
    }

    cout << "Ready! Press INSERT to toggle" << endl;

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        cout << "Failed to open X display!" << endl;
        return 1;
    }

    bool insertPressed = false;
    int currentValue = 1;

    // Enable wallhack immediately upon finding the address
    int enableValue = 2;
    if (writeMemory(pid, address, &enableValue, sizeof(enableValue))) {
        currentValue = 2;
        cout << "Wallhack: ON" << endl;
    } else {
        cout << "Wallhack: OFF" << endl;
    }

    while (true) {
        if (!readMemory(pid, address, &currentValue, sizeof(currentValue))) {
            cout << "\nGame closed. Exiting..." << endl;
            break;
        }

        char keys[32];
        XQueryKeymap(display, keys);

        KeyCode insertKey = XKeysymToKeycode(display, XK_Insert);
        bool insertCurrentlyPressed = (keys[insertKey / 8] & (1 << (insertKey % 8))) != 0;

        if (insertCurrentlyPressed && !insertPressed) {
            int newValue = (currentValue == 1) ? 2 : 1;
            if (writeMemory(pid, address, &newValue, sizeof(newValue))) {
                currentValue = newValue;
                cout << "\rWallhack: " << (newValue == 2 ? "ON " : "OFF") << flush;
            }
        }
        insertPressed = insertCurrentlyPressed;

        usleep(10000);
    }

    XCloseDisplay(display);

    return 0;
}