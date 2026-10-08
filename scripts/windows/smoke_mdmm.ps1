# Starts each standalone of the Windows package, and each VST3 in a minimal host (scripts/vst3EditorHost, when -Vst3Host
# is given), (elektron-windows.yml's Gearmulator-Elektron-Windows-x64.zip,
# unpacked) with no ROM and a scratch data root (GEARMULATOR_DATA_ROOT), and checks what can be checked without a person (doc/release/WINDOWS.md):
#   - the app is still running after a while;
#   - its page runs in WebView2 (msedgewebview2.exe processes with the editors' profile folder), not in the
#     Internet Explorer control;
#   - the bridge went both ways: the page said "ready" (page -> plug-in, postMessage), the plug-in answered with
#     the machine's state, and the page shows "<machine> firmware needed" (plug-in -> page, ExecuteScript). The
#     text is read through UI Automation (the page's accessibility tree), so the shipped build needs no log;
#   - real keys reach the page: the window in front, a real click on the page, then Ctrl+C Ctrl+V Ctrl+X Ctrl+Z Ctrl+D
#     and ? as real key presses, and the page's key probe (GEARMULATOR_MDMM_KEYPROBE=1) lists them;
#   - B-022: each run wrote its start-up log (<data root>\<machine>\logs\editor-*.log: the WebView2 runtime's version,
#     the bridge up), copied to <name>-editor.log; the Machinedrum standalone runs three more times: as with an old
#     runtime (GEARMULATOR_MDMM_WEBVIEW2_TEST=old: no ICoreWebView2Settings3, the page must still work), as on a machine
#     without one (=fail) and with no page (GEARMULATOR_MDMM_PAGE_TEST=nostart): those two must say in the window
#     that the editor page could not start (read through UI Automation);
#   - a screenshot of the screen (md-standalone.png, md-vst3.png, ...; an artifact, to look at) and summary.md.
#
#   scripts/windows/smoke_mdmm.ps1 -PackageDir <unpacked zip> -OutputDir <dir> [-Vst3Host <mdmmVst3EditorHost.exe>]
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $PackageDir,
    [Parameter(Mandatory = $true)] [string] $OutputDir,
    [string] $Vst3Host = '',
    [int] $TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms, System.Drawing

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
# A scratch data root (ROM and settings folders), as on macOS and Linux: nothing of the run stays.
$dataRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mdmm-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dataRoot -Force | Out-Null
$env:GEARMULATOR_DATA_ROOT = $dataRoot + '\'

function Save-Screenshot([string] $Path) {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

# Every name in the UI Automation tree under the app's windows (the WebView2 page included).
function Get-UiNames([int] $ProcessId) {
    $A = [System.Windows.Automation.AutomationElement]
    $condition = New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, $ProcessId)
    $names = New-Object System.Collections.Generic.List[string]
    $windows = $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)
    foreach ($window in $windows) {
        $names.Add("[window] " + $window.Current.Name)
        $all = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($element in $all) {
            try {
                $name = $element.Current.Name
                if ($name) { $names.Add("[" + $element.Current.ControlType.ProgrammaticName + "] " + $name) }
            } catch { }
        }
    }
    return $names
}

# Real input (0.3.3 lost Cmd+C / Cmd+V on macOS; this proves Ctrl+C and the rest reach the page here too): the window
# in front, a real click on the page's key probe (skins/shared/deskKeys.js, GEARMULATOR_MDMM_KEYPROBE=1), then real key
# presses (keybd_event: the system's input queue, so the host window, the WebView2 controller and its accelerator keys
# take their turns). Returns the probe's text afterwards.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MdmmInput {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
    public static void Click(int x, int y) { SetCursorPos(x, y); mouse_event(2, 0, 0, 0, UIntPtr.Zero); mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
    public static void Key(byte vk, bool down) { keybd_event(vk, 0, down ? 0u : 2u, UIntPtr.Zero); }
}
'@
function Find-KeyProbe([int] $ProcessId) {
    $A = [System.Windows.Automation.AutomationElement]
    $condition = New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, $ProcessId)
    foreach ($window in $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)) {
        foreach ($element in $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
            try { if ($element.Current.Name -like 'Keys seen:*') { return $element } } catch { }
        }
    }
    return $null
}
function Send-RealKeys($Process) {
    $probe = Find-KeyProbe -ProcessId $Process.Id
    if (-not $probe) { return 'no key probe on the page' }
    $Process.Refresh()
    [MdmmInput]::SetForegroundWindow($Process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 500
    $r = $probe.Current.BoundingRectangle
    [MdmmInput]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
    Start-Sleep -Milliseconds 500
    # Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+Z, Ctrl+D, then Shift+/ (?)
    foreach ($key in @(@(0x11, 0x43), @(0x11, 0x56), @(0x11, 0x58), @(0x11, 0x5A), @(0x11, 0x44), @(0x10, 0xBF))) {
        [MdmmInput]::Key([byte]$key[0], $true); [MdmmInput]::Key([byte]$key[1], $true)
        Start-Sleep -Milliseconds 50
        [MdmmInput]::Key([byte]$key[1], $false); [MdmmInput]::Key([byte]$key[0], $false)
        Start-Sleep -Milliseconds 400
    }
    Start-Sleep -Milliseconds 500
    $probe = Find-KeyProbe -ProcessId $Process.Id
    if ($probe) { return $probe.Current.Name }
    return 'the key probe went'
}
$env:GEARMULATOR_MDMM_KEYPROBE = '1'
$keysWanted = @('cmd+C', 'cmd+V', 'cmd+X', 'cmd+Z', 'cmd+D', 'shift+?')

function Get-WebViewProcesses() {
    return @(Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" -ErrorAction SilentlyContinue)
}

$productNames = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\mdmm-product.env')) {
    if ($line -match '^(MDMM_[A-Z_]+)="(.*)"$') { $productNames[$Matches[1]] = $Matches[2] }
}
$profileFolder = Join-Path $env:LOCALAPPDATA 'Gearmulator\EditorWebView2'
$runs = New-Object System.Collections.Generic.List[object]
foreach ($machine in @(@{ Product = $productNames['MDMM_PRODUCT_NAME_MD']; Machine = 'Machinedrum'; Name = 'md' },
                       @{ Product = $productNames['MDMM_PRODUCT_NAME_MM']; Machine = 'Monomachine'; Name = 'mm' })) {
    $exe = Get-ChildItem -LiteralPath $PackageDir -Recurse -File -Filter "$($machine.Product).exe" | Select-Object -First 1
    if (-not $exe) { throw "Not in the package: $($machine.Product).exe" }
    $runs.Add(@{ Label = "$($machine.Product) standalone"; File = $exe.FullName; Arguments = @(); Machine = $machine.Machine;
        Name = "$($machine.Name)-standalone" })
    if ($Vst3Host) {
        $bundle = Get-ChildItem -LiteralPath $PackageDir -Recurse -Directory -Filter "$($machine.Product).vst3" | Select-Object -First 1
        if (-not $bundle) { throw "Not in the package: $($machine.Product).vst3" }
        $runs.Add(@{ Label = "$($machine.Product) VST3 in mdmmVst3EditorHost"; File = $Vst3Host;
            Arguments = @("`"$($bundle.FullName)`"", "$($TimeoutSeconds + 30)"); Machine = $machine.Machine;
            Name = "$($machine.Name)-vst3" })
    }
}

# B-022: the fallback and failure paths, on the Machinedrum standalone (any build: the switches are read by the shipped one)
$mdExe = $runs[0].File
$runs.Add(@{ Label = 'MD standalone, old WebView2 runtime (test)'; File = $mdExe; Arguments = @(); Machine = 'Machinedrum'; Name = 'md-webview2-old'
    Env = @{ GEARMULATOR_MDMM_WEBVIEW2_TEST = 'old' }; LogWants = 'ICoreWebView2Settings3 not available' })
$runs.Add(@{ Label = 'MD standalone, no WebView2 runtime (test)'; File = $mdExe; Arguments = @(); Machine = 'Machinedrum'; Name = 'md-webview2-fail'
    Env = @{ GEARMULATOR_MDMM_WEBVIEW2_TEST = 'fail' }; Failure = $true })
$runs.Add(@{ Label = 'MD standalone, page that never starts (test)'; File = $mdExe; Arguments = @(); Machine = 'Machinedrum'; Name = 'md-page-nostart'
    Env = @{ GEARMULATOR_MDMM_PAGE_TEST = 'nostart' }; Failure = $true })

$status = 0
$summary = New-Object System.Collections.Generic.List[string]
function Add-Row([string] $What, [string] $Check, [string] $Result) { $summary.Add("| $What | $Check | $Result |") }
foreach ($run in $runs) {
    Write-Host "== $($run.Label): $($run.File) $($run.Arguments -join ' ')"
    $start = @{ FilePath = $run.File; WorkingDirectory = (Split-Path -Parent $run.File); PassThru = $true
        RedirectStandardOutput = (Join-Path $OutputDir "$($run.Name)-stdout.txt")
        RedirectStandardError = (Join-Path $OutputDir "$($run.Name)-stderr.txt") }
    if ($run.Arguments.Count -gt 0) { $start.ArgumentList = $run.Arguments }
    $runEnv = if ($run.ContainsKey('Env')) { $run.Env } else { @{} }
    foreach ($k in $runEnv.Keys) { Set-Item -Path "env:$k" -Value $runEnv[$k] }
    Get-ChildItem -LiteralPath $dataRoot -Recurse -File -Filter 'editor-*.log' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    $process = Start-Process @start
    foreach ($k in $runEnv.Keys) { Remove-Item -Path "env:$k" -ErrorAction SilentlyContinue }
    $failureRun = $run.ContainsKey('Failure') -and $run.Failure
    $wanted = if ($failureRun) { 'The editor page could not start' } else { "$($run.Machine) firmware needed" }
    $found = $false
    $names = @()
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        if ($process.HasExited) { break }
        try { $names = @(Get-UiNames -ProcessId $process.Id) } catch { $names = @("UI Automation: $_") }
        if ($names | Where-Object { $_ -like "*$wanted*" }) { $found = $true; break }
    }
    Start-Sleep -Seconds 2
    Save-Screenshot (Join-Path $OutputDir "$($run.Name).png")
    $names | Set-Content -LiteralPath (Join-Path $OutputDir "$($run.Name)-ui.txt") -Encoding UTF8
    $webviews = Get-WebViewProcesses
    $webviews | ForEach-Object { "$($_.ProcessId) $($_.ParentProcessId) $($_.CommandLine)" } |
        Set-Content -LiteralPath (Join-Path $OutputDir "$($run.Name)-webview-processes.txt") -Encoding UTF8
    $ours = @($webviews | Where-Object { $_.CommandLine -and $_.CommandLine -like '*Gearmulator\EditorWebView2*' })

    if ($process.HasExited) {
        Write-Host "::error::$($run.Label) exited (code $($process.ExitCode))"
        Add-Row $run.Label 'running' "no: exited ($($process.ExitCode))"
        Get-Content -LiteralPath (Join-Path $OutputDir "$($run.Name)-stderr.txt") -ErrorAction SilentlyContinue
        $status = 1
        continue
    }
    Write-Host "$($run.Label): running"
    Add-Row $run.Label 'running' 'yes'
    # B-022: the start-up log a user can send
    $startupLog = Get-ChildItem -LiteralPath $dataRoot -Recurse -File -Filter 'editor-*.log' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike '*-previous.log' } | Select-Object -First 1
    $logText = ''
    if ($startupLog) {
        Copy-Item -LiteralPath $startupLog.FullName -Destination (Join-Path $OutputDir "$($run.Name)-editor.log") -Force
        $logText = Get-Content -LiteralPath $startupLog.FullName -Raw
    }
    $logWanted = @('WebView2 runtime')
    if (-not $failureRun) { $logWanted += 'page up' }
    if ($run.ContainsKey('LogWants')) { $logWanted += $run.LogWants }
    if ($failureRun) { $logWanted += 'FAILED:' }
    $logMissing = @($logWanted | Where-Object { -not $logText.Contains($_) })
    if ($startupLog -and $logMissing.Count -eq 0) {
        Write-Host "$($run.Label): start-up log $($startupLog.FullName)"
        Add-Row $run.Label "start-up log ($($logWanted -join ', '))" 'yes'
    } else {
        Write-Host "::error::$($run.Label): start-up log missing or without: $($logMissing -join ', ') ($($startupLog))"
        Add-Row $run.Label "start-up log ($($logWanted -join ', '))" "no: $($logMissing -join ', ')"
        $status = 1
    }
    if ($failureRun) {
        if ($found) {
            Write-Host "$($run.Label): the window says '$wanted'"
            Add-Row $run.Label "window says `"$wanted`"" 'yes'
        } else {
            Write-Host "::error::$($run.Label): the window never said '$wanted' within $TimeoutSeconds s"
            Add-Row $run.Label "window says `"$wanted`"" 'no'
            $names | Select-Object -First 80 | ForEach-Object { Write-Host "   $_" }
            $status = 1
        }
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
        Get-WebViewProcesses | Where-Object { $_.CommandLine -like '*Gearmulator\EditorWebView2*' } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Seconds 1
        continue
    }
    if ($ours.Count -gt 0) {
        Write-Host "$($run.Label): WebView2 is running with the editors' profile ($($ours.Count) msedgewebview2.exe processes)"
        Add-Row $run.Label 'WebView2 page process' "yes ($($ours.Count))"
    } else {
        Write-Host "::error::$($run.Label): no msedgewebview2.exe with the profile $profileFolder"
        Add-Row $run.Label 'WebView2 page process' 'no'
        $status = 1
    }
    if ($found) {
        Write-Host "$($run.Label): the page shows '$wanted' (page -> plug-in -> page)"
        Add-Row $run.Label "page shows `"$wanted`"" 'yes'
    } else {
        Write-Host "::error::$($run.Label): the page never showed '$wanted' within $TimeoutSeconds s"
        Add-Row $run.Label "page shows `"$wanted`"" 'no'
        Write-Host "-- names in the UI Automation tree (first 80)"
        $names | Select-Object -First 80 | ForEach-Object { Write-Host "   $_" }
        $status = 1
    }
    if ($found) {
        $seen = ''
        try { $seen = Send-RealKeys -Process $process } catch { $seen = "input: $_" }
        $seen | Set-Content -LiteralPath (Join-Path $OutputDir "$($run.Name)-keys.txt") -Encoding UTF8
        $missing = @($keysWanted | Where-Object { -not $seen.Contains(" $_") })
        if ($missing.Count -eq 0) {
            Write-Host "$($run.Label): real keys reach the page: $seen"
            Add-Row $run.Label "real keys reach the page ($($keysWanted -join ' '))" 'yes'
        } else {
            Write-Host "::error::$($run.Label): real keys did not reach the page: $($missing -join ' ') missing ($seen)"
            Add-Row $run.Label "real keys reach the page ($($keysWanted -join ' '))" "no: $($missing -join ' ') missing"
            $status = 1
        }
    }
    if (-not (Test-Path -LiteralPath $profileFolder)) {
        Write-Host "::error::no WebView2 profile folder at $profileFolder"
        $status = 1
    }
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
    Get-WebViewProcesses | Where-Object { $_.CommandLine -like '*Gearmulator\EditorWebView2*' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
}
Get-ChildItem -LiteralPath $env:TEMP -Filter 'gearmulator-*' -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host "temp: $($_.Name) $($_.Length) bytes" }
$os = (Get-CimInstance Win32_OperatingSystem).Caption
$table = @("### Windows start test ($os)", '', '| What | Check | Result |', '|---|---|---|') + $summary
$table | Set-Content -LiteralPath (Join-Path $OutputDir 'summary.md') -Encoding UTF8
$table | ForEach-Object { Write-Host $_ }
if ($env:GITHUB_STEP_SUMMARY) { $table | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding UTF8 }
Remove-Item -LiteralPath $dataRoot -Recurse -Force -ErrorAction SilentlyContinue
exit $status
