# Sample WDDM process memory for each adapter visible to a running benchmark.
param(
    [Parameter(Mandatory = $true)][int] $ProcessId,
    [Parameter(Mandatory = $true)][string] $OutputPath
)

$ErrorActionPreference = 'Stop'
$benchmark = Get-Process -Id $ProcessId
$category = [System.Diagnostics.PerformanceCounterCategory]::new('GPU Process Memory')
$adapters = @($category.GetInstanceNames() | Where-Object { $_ -like "pid_${ProcessId}_*" })
if ($adapters.Count -eq 0) {
    throw "No WDDM GPU process memory counters for PID $ProcessId. Start sampling after the Vulkan device initializes."
}

$counters = foreach ($adapter in $adapters) {
    [pscustomobject]@{
        Instance = $adapter
        Dedicated = [System.Diagnostics.PerformanceCounter]::new('GPU Process Memory', 'Dedicated Usage', $adapter, $true)
        Shared = [System.Diagnostics.PerformanceCounter]::new('GPU Process Memory', 'Shared Usage', $adapter, $true)
    }
}

$writer = [System.IO.StreamWriter]::new($OutputPath)
try {
    $writer.WriteLine('unix_ms,adapter_instance,dedicated_bytes,shared_bytes')
    while (-not $benchmark.HasExited) {
        $timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        foreach ($counter in $counters) {
            try {
                $dedicated = $counter.Dedicated.RawValue
                $shared = $counter.Shared.RawValue
                $writer.WriteLine("$timestamp,$($counter.Instance),$dedicated,$shared")
            } catch {
                if (-not $benchmark.HasExited) {
                    throw
                }
            }
        }
        $writer.Flush()
        Start-Sleep -Seconds 1
    }
} finally {
    $writer.Dispose()
    foreach ($counter in $counters) {
        $counter.Dedicated.Dispose()
        $counter.Shared.Dispose()
    }
    $benchmark.Dispose()
}
