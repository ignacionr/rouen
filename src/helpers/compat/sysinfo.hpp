#pragma once

// Platform-specific implementation for system information
// This file provides macOS compatibility for Linux's sys/sysinfo.h

#include <cstdint>
#include <sys/types.h>

#ifdef __APPLE__
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <mach/vm_statistics.h>

    // Define a sysinfo struct similar to Linux's for macOS
    struct sysinfo {
        long uptime;             // Seconds since boot
        unsigned long loads[3];  // 1, 5, and 15 minute load averages
        unsigned long totalram;  // Total usable main memory size
        unsigned long freeram;   // Available memory size
        unsigned long sharedram; // Amount of shared memory
        unsigned long bufferram; // Memory used by buffers
        unsigned long totalswap; // Total swap space size
        unsigned long freeswap;  // Swap space still available
        unsigned short procs;    // Number of current processes
        unsigned long totalhigh; // Total high memory size
        unsigned long freehigh;  // Available high memory size
        unsigned int mem_unit;   // Memory unit size in bytes
    };

    // Implementation of sysinfo for macOS
    inline int sysinfo(struct sysinfo *info) {
        if (!info) return -1;
        
        // Initialize the structure with zeros
        std::memset(info, 0, sizeof(struct sysinfo));
        
        // Set memory unit to bytes
        info->mem_unit = 1;
        
        // Get system uptime
        struct timeval boottime;
        size_t len = sizeof(boottime);
        int mib[2] = { CTL_KERN, KERN_BOOTTIME };
        if (sysctl(mib, 2, &boottime, &len, NULL, 0) >= 0) {
            time_t now = time(NULL);
            info->uptime = now - boottime.tv_sec;
        }
        
        // Get memory information
        vm_size_t page_size;
        mach_port_t mach_port = mach_host_self();
        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);
        
        if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
            host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
            
            // Get total physical memory
            int mib_total[2] = { CTL_HW, HW_MEMSIZE };
            uint64_t total_memory;
            size_t total_len = sizeof(total_memory);
            if (sysctl(mib_total, 2, &total_memory, &total_len, NULL, 0) >= 0) {
                info->totalram = total_memory;
            }
            
            // Calculate free memory (free pages + inactive pages) * page size
            info->freeram = (vm_stats.free_count + vm_stats.inactive_count) * page_size;
            
            // Shared memory (file-backed pages) * page size
            info->sharedram = vm_stats.external_page_count * page_size;
            
            // Wired memory as buffer equivalent
            info->bufferram = vm_stats.wire_count * page_size;
        }
        
        // Get swap information
        struct xsw_usage swap_info;
        len = sizeof(swap_info);
        mib[0] = CTL_VM;
        mib[1] = VM_SWAPUSAGE;
        if (sysctl(mib, 2, &swap_info, &len, NULL, 0) >= 0) {
            info->totalswap = swap_info.xsu_total;
            info->freeswap = swap_info.xsu_avail;
        }
        
        // Get process count
        int mib_proc[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
        size_t proc_len;
        if (sysctl(mib_proc, 3, NULL, &proc_len, NULL, 0) >= 0) {
            // Calculate process count with careful casting to avoid precision loss
            size_t proc_count = proc_len / sizeof(struct kinfo_proc);
            // Ensure we don't exceed the range of unsigned short
            info->procs = (proc_count > USHRT_MAX) ? USHRT_MAX : static_cast<unsigned short>(proc_count);
        }
        
        return 0;
    }
#endif // __APPLE__