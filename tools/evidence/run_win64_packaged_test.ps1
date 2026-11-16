# run_win64_packaged_test.ps1
# Launches the staged Win64 package, samples it for N seconds, then closes it the way a
# user would (WM_CLOSE on the game window) so we can prove the package starts, ticks and
# shuts down cleanly. Writes the engine log plus a sampling JSON used by
# gen_win64_evidence.ps1. No value is assumed: everything printed here is measured.
param(
    [string]$ExePath   = 'F:\CG\Projects\PICOUE\artifacts\Win64Package\PICOHost59\Binaries\Win64\PICOHost59.exe',
    [string]$Map       = '/Game/Maps/PICOTestMap',
    [string]$LogPath   = 'F:\CG\Projects\PICOUE\artifacts\DELIVERY\VerifyReports\win64_packaged_run.log',
    [string]$SamplePath= 'F:\CG\Projects\PICOUE\artifacts\win64_run_sampling.json',
    [int[]]$SampleAt   = @(10, 20, 30, 45, 60),
    [int]$CloseTimeoutSec = 60
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ExePath)) { throw "exe not found: $ExePath" }
if (Test-Path -LiteralPath $LogPath) { Remove-Item -LiteralPath $LogPath -Force }

$exeArgs = @($Map, '-game', '-log', ("-abslog=" + $LogPath),
             '-nosplash', '-NoSound', '-nop4', '-windowed', '-ResX=1280', '-ResY=720')
$proc = Start-Process -FilePath $ExePath -ArgumentList $exeArgs -PassThru
Write-Output ("PID=" + $proc.Id)

$rows = @()
$prev = 0
foreach ($t in $SampleAt) {
    $wait = $t - $prev
    if ($wait -gt 0) { Start-Sleep -Seconds $wait }
    $prev = $t
    $proc.Refresh()
    if ($proc.HasExited) {
        Write-Output ("t={0,3}s EXITED code={1}" -f $t, $proc.ExitCode)
        $rows += [pscustomobject]@{ sec = $t; exited = $true; exitCode = $proc.ExitCode }
        break
    }
    $bytes = 0
    if (Test-Path -LiteralPath $LogPath) { $bytes = (Get-Item -LiteralPath $LogPath).Length }
    $row = [pscustomobject]@{
        sec    = $t
        exited = $false
        cpuSec = [math]::Round($proc.TotalProcessorTime.TotalSeconds, 2)
        logBytes = $bytes
        wsMb   = [int]($proc.WorkingSet64 / 1MB)
    }
    $rows += $row
    Write-Output ("t={0,3}s alive=True cpu={1,7:N2}s logBytes={2,9:N0} WS={3,5}MB" -f $t, $row.cpuSec, $row.logBytes, $row.wsMb)
}

# Close it the way the user would: WM_CLOSE to the game window, not Stop-Process.
$graceful = $false
$exitCode = $null
if (-not $proc.HasExited) {
    $graceful = $proc.CloseMainWindow()
    $exited = $proc.WaitForExit($CloseTimeoutSec * 1000)
    Write-Output ("CloseMainWindow={0} exitedGracefully={1}" -f $graceful, $exited)
    if ($exited) { $exitCode = $proc.ExitCode; Write-Output ("exitCode={0}" -f $exitCode) }
    else { Stop-Process -Id $proc.Id -Force; Write-Output 'FORCE_KILLED' }
} else {
    $exitCode = $proc.ExitCode
}

$result = [pscustomobject]@{
    exe        = $ExePath
    exeBytes   = (Get-Item -LiteralPath $ExePath).Length
    exeSha256  = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash
    exeMtime   = (Get-Item -LiteralPath $ExePath).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
    map        = $Map
    logPath    = $LogPath
    logBytes   = if (Test-Path -LiteralPath $LogPath) { (Get-Item -LiteralPath $LogPath).Length } else { 0 }
    samples    = $rows
    closeSent  = $graceful
    exitCode   = $exitCode
    sampledAt  = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $SamplePath -Encoding utf8NoBOM
Write-Output ("sampling -> " + $SamplePath)
