# Starts each standalone of the Windows package, and each VST3 in a minimal host (vst3EditorHost/, when -Vst3Host
# is given), (elektron-windows.yml's Gearmulator-Elektron-Windows-x64.zip,
# unpacked) with no ROM and checks what can be checked without a person (doc/release/WINDOWS.md):
#   - the app is still running after a while;
#   - its page runs in WebView2 (msedgewebview2.exe processes with the editors' profile folder), not in the
#     Internet Explorer control;
#   - the bridge went both ways: the page said "ready" (page -> plug-in, postMessage), the plug-in answered with
#     the machine's state, and the page shows "<machine> firmware needed" (plug-in -> page, ExecuteScript). The
#     text is read through UI Automation (the page's accessibility tree), so the shipped build needs no log;
#   - a screenshot of the screen (an artifact, to look at).
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

$status = 0
foreach ($run in $runs) {
    Write-Host "== $($run.Label): $($run.File) $($run.Arguments -join ' ')"
    $start = @{ FilePath = $run.File; WorkingDirectory = (Split-Path -Parent $run.File); PassThru = $true
        RedirectStandardOutput = (Join-Path $OutputDir "$($run.Name)-stdout.txt")
        RedirectStandardError = (Join-Path $OutputDir "$($run.Name)-stderr.txt") }
    if ($run.Arguments.Count -gt 0) { $start.ArgumentList = $run.Arguments }
    $process = Start-Process @start
    $wanted = "$($run.Machine) firmware needed"
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
        Get-Content -LiteralPath (Join-Path $OutputDir "$($run.Name)-stderr.txt") -ErrorAction SilentlyContinue
        $status = 1
        continue
    }
    Write-Host "$($run.Label): running"
    if ($ours.Count -gt 0) {
        Write-Host "$($run.Label): WebView2 is running with the editors' profile ($($ours.Count) msedgewebview2.exe processes)"
    } else {
        Write-Host "::error::$($run.Label): no msedgewebview2.exe with the profile $profileFolder"
        $status = 1
    }
    if ($found) {
        Write-Host "$($run.Label): the page shows '$wanted' (page -> plug-in -> page)"
    } else {
        Write-Host "::error::$($run.Label): the page never showed '$wanted' within $TimeoutSeconds s"
        Write-Host "-- names in the UI Automation tree (first 80)"
        $names | Select-Object -First 80 | ForEach-Object { Write-Host "   $_" }
        $status = 1
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
exit $status
