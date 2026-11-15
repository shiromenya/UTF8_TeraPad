$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildObj = Join-Path $root 'build\obj'
$buildLib = Join-Path $root 'build\lib'
$distDir  = Join-Path $root 'dist'
$testEnv  = Join-Path $root 'test_env'

New-Item -ItemType Directory -Force -Path $buildObj, $buildLib, $distDir | Out-Null

$libs = @('kernel32', 'user32', 'gdi32', 'imm32')
foreach ($lib in $libs) {
    $defFile = Join-Path $root "src\libs\${lib}_min.def"
    $aFile   = Join-Path $buildLib "lib${lib}.a"
    & dlltool -m i386 --as-flags=--32 -k -d $defFile -l $aFile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$cflags = @('-m32', '-O2', '-Wall', '-Wextra', '-fno-builtin', '-DWIN32_LEAN_AND_MEAN', '-c')
$sources = @('version_proxy.c', 'iat_hook.c', 'utf8_util.c', 'hooks_text.c', 'hooks_input.c', 'main.c')

Write-Host 'Compiling 32-bit objects...'
$objs = @()
foreach ($src in $sources) {
    $obj = Join-Path $buildObj ($src -replace '\.c$', '.o')
    & gcc @cflags (Join-Path $root "src\$src") -o $obj
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += $obj
}

Write-Host 'Linking 32-bit version.dll...'
$outDll = Join-Path $distDir 'version.dll'
$ldArgs = @(
    '-m', 'i386pe', '--shared', '--enable-stdcall-fixup',
    '-e', '_DllMain@12',
    (Join-Path $root 'src\version_proxy.def')
) + $objs + @(
    (Join-Path $buildLib 'libkernel32.a'),
    (Join-Path $buildLib 'libuser32.a'),
    (Join-Path $buildLib 'libgdi32.a'),
    (Join-Path $buildLib 'libimm32.a'),
    '-o', $outDll
)

& ld @ldArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path $testEnv) {
    Copy-Item $outDll (Join-Path $testEnv 'version.dll') -Force
}

Write-Host "Built: $outDll"
