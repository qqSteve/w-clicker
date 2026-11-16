[CmdletBinding()]
param(
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Initialize-Msvc {
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'MSVC not found. Install Visual Studio 2022 Build Tools with Desktop development with C++.'
    }

    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) {
        throw 'No Visual Studio installation with C++ x64 tools was found.'
    }

    $devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    $bootstrap = Join-Path ([IO.Path]::GetTempPath()) ('w-clicker-msvc-' + [Guid]::NewGuid().ToString('N') + '.cmd')
    try {
        @(
            '@echo off'
            ('call "' + $devCmd + '" -no_logo -arch=x64 -host_arch=x64 >nul')
            'if errorlevel 1 exit /b %errorlevel%'
            'set'
        ) | Set-Content -LiteralPath $bootstrap -Encoding Default

        $environmentLines = & $env:ComSpec /d /c ('"' + $bootstrap + '"')
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to initialize the Visual Studio build environment.'
        }
        foreach ($line in $environmentLines) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) {
                [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
            }
        }
    }
    finally {
        Remove-Item -LiteralPath $bootstrap -ErrorAction SilentlyContinue
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'cl.exe is unavailable after initializing Visual Studio.'
    }
}

function Invoke-Compiler {
    param([string[]]$CompilerArguments)
    & cl.exe @CompilerArguments
    if ($LASTEXITCODE -ne 0) {
        throw ('C++ compilation failed with exit code ' + $LASTEXITCODE)
    }
}

Initialize-Msvc
$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $repositoryRoot
try {
    New-Item -ItemType Directory -Path 'dist' -Force | Out-Null
    $common = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/utf-8', '/DUNICODE', '/D_UNICODE', '/O2', '/MT', '/Isrc')

    Invoke-Compiler -CompilerArguments ($common + @(
        'src/main.cpp', '/Fodist/main.obj', '/Fedist/WClicker.exe',
        '/link', 'user32.lib', 'gdi32.lib', '/SUBSYSTEM:WINDOWS'
    ))

    if (-not $SkipTests) {
        Invoke-Compiler -CompilerArguments ($common + @(
            'tests/engine_test.cpp', '/Fodist/engine_test.obj', '/Fedist/engine_test.exe'
        ))
        & '.\dist\engine_test.exe'
        if ($LASTEXITCODE -ne 0) {
            throw ('Engine tests failed with exit code ' + $LASTEXITCODE)
        }

        if (Test-Path -LiteralPath 'tests/win32_smoke.cpp') {
            Invoke-Compiler -CompilerArguments ($common + @(
                'tests/win32_smoke.cpp', '/Fodist/win32_smoke.obj', '/Fedist/win32_smoke.exe',
                '/link', 'user32.lib', 'gdi32.lib'
            ))
            & '.\dist\win32_smoke.exe'
            if ($LASTEXITCODE -ne 0) {
                throw ('Windows smoke tests failed with exit code ' + $LASTEXITCODE)
            }
        }

        $testProcess = $null
        try {
            $testProcess = Start-Process -FilePath (Join-Path $repositoryRoot 'dist/WClicker.exe') -PassThru
            $windowTimeout = [Diagnostics.Stopwatch]::StartNew()
            do {
                $testProcess.Refresh()
                if ($testProcess.HasExited) {
                    throw ('WClicker exited before opening its window; exit code ' + $testProcess.ExitCode)
                }
                if ($testProcess.MainWindowHandle -ne [IntPtr]::Zero) {
                    break
                }
                Start-Sleep -Milliseconds 50
            } while ($windowTimeout.ElapsedMilliseconds -lt 5000)

            $testProcess.Refresh()
            if ($testProcess.MainWindowHandle -eq [IntPtr]::Zero) {
                throw 'WClicker did not open its main window within 5 seconds.'
            }
            if ($testProcess.WaitForExit(200)) {
                throw ('WClicker exited unexpectedly after opening its window; exit code ' + $testProcess.ExitCode)
            }
            if (-not $testProcess.CloseMainWindow()) {
                throw 'WClicker did not accept a normal window close request.'
            }
            if (-not $testProcess.WaitForExit(5000)) {
                throw 'WClicker did not exit within 5 seconds after closing its window.'
            }
            if ($testProcess.ExitCode -ne 0) {
                throw ('WClicker GUI smoke test failed with exit code ' + $testProcess.ExitCode)
            }
            Write-Host 'GUI lifecycle smoke test passed.'
        }
        finally {
            if ($null -ne $testProcess) {
                try {
                    $testProcess.Refresh()
                    if (-not $testProcess.HasExited) {
                        $testProcess.Kill()
                        [void]$testProcess.WaitForExit(5000)
                    }
                }
                finally {
                    $testProcess.Dispose()
                }
            }
        }
    }

    $checksum = (Get-FileHash -LiteralPath 'dist/WClicker.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
    ($checksum + '  WClicker.exe') | Set-Content -LiteralPath 'dist/SHA256SUMS.txt' -Encoding ASCII
    Write-Host 'Build complete: dist\WClicker.exe'
}
finally {
    Pop-Location
}
