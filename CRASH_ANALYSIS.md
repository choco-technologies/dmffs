# Segmentation Fault Analysis - DMOD API Connection Crash

## Problem Summary
The dmffs module crashes when loaded through fs_tester/dmvfs, but works correctly when loaded through dmod_loader. The crash occurs during API connection, specifically in `Dmod_ConnectApi()`.

## Root Cause
The crash happens in `src/system/public/dmod_dmf_api.c` in the `Dmod_ConnectApi()` function when iterating through API entries to connect them. The crash occurs when accessing `InputsApi->InputSection->Entries[j].Signature` during the connection loop.

### Evidence
```
[VERBOSE] Dmod_ConnectApi: Connecting 6 outputs to 61 inputs
[VERBOSE] Processing output 1/6
[VERBOSE] Connected: DMOD_IsFunctionConnected@Dmod:1.0 0x0040A764
[VERBOSE] Processing output 2/6
[VERBOSE] Connected: DMOD_Printf@Dmod...
<< SEGFAULT HERE >>
```

The system is trying to connect 6 output APIs to 61 input APIs. It successfully connects the first API (IsFunctionConnected), starts connecting the second (Printf), and then crashes.

## Technical Details

### Location
File: `src/system/public/dmod_dmf_api.c`
Function: `Dmod_ConnectApi()`
Line: Around line 36-42 in the nested loop

### Crash Scenario
1. Module loading completes successfully
2. API connection phase begins
3. First output API connects successfully  
4. While processing second output API and iterating through input APIs
5. Crash when accessing `InputsApi->InputSection->Entries[j].Signature`

### Why It Works in dmod_loader but Not fs_tester
- **dmod_loader**: Uses simpler module loading, cleaner memory layout
- **fs_tester/dmvfs**: More complex environment with multiple modules (dmvfs, dmfsi, dmffs), potential memory layout issues

## Proposed Fix

Add safety checks before accessing potentially corrupt memory:

```c
// In Dmod_ConnectApi() function:

// 1. Validate API counts
if (numberOfOutputs > 1000 || numberOfInputs > 1000) {
    DMOD_LOG_ERROR("Unreasonable API counts detected\n");
    return false;
}

// 2. Validate pointers before dereferencing
if (OutputsApi->OutputSection == NULL || InputsApi->InputSection == NULL ||
    OutputsApi->OutputSection->Entries == NULL || InputsApi->InputSection->Entries == NULL) {
    DMOD_LOG_ERROR("API section pointers are NULL\n");
    return false;
}

// 3. Skip NULL signatures in the loop
for(size_t j = 0; j < numberOfInputs; j++) {
    if (InputsApi->InputSection->Entries[j].Signature == NULL) {
        continue;  // Skip this entry instead of crashing
    }
    // ... rest of connection logic
}
```

## Files Affected
- `src/system/public/dmod_dmf_api.c` - Main fix location

## Testing
After applying the patch:
1. Build dmvfs with the patched dmod
2. Run: `fs_tester --read-only-fs --test-file /mnt/hello.txt --test-dir /mnt dmffs.dmf`
3. Verify the crash no longer occurs and all APIs connect successfully

## Patch File
See `dmod_crash_fix.patch` for the complete fix that can be applied to the dmod repository.
