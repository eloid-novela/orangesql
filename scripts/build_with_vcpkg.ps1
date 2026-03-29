# Script para build no Windows com vcpkg

Write-Host "🍊 Configurando OrangeSQL para Windows..." -ForegroundColor Blue

# Verificar prerequisites
function Check-Command($cmdname) {
    if (Get-Command $cmdname -ErrorAction SilentlyContinue) {
        Write-Host "✅ $cmdname encontrado" -ForegroundColor Green
        return $true
    } else {
        Write-Host "❌ $cmdname não encontrado" -ForegroundColor Red
        return $false
    }
}

# Verificar cmake
if (-not (Check-Command "cmake")) {
    Write-Host "Por favor, instale CMake primeiro: https://cmake.org/download/" -ForegroundColor Red
    exit 1
}

# Verificar git
if (-not (Check-Command "git")) {
    Write-Host "Por favor, instale Git primeiro: https://git-scm.com/download/win" -ForegroundColor Red
    exit 1
}

# Verificar vcpkg
$VCPKG_ROOT = $env:VCPKG_ROOT
if (-not $VCPKG_ROOT) {
    $VCPKG_ROOT = "$PSScriptRoot\vcpkg"
    Write-Host "VCPKG_ROOT não definido, usando: $VCPKG_ROOT" -ForegroundColor Yellow
    
    if (-not (Test-Path $VCPKG_ROOT)) {
        Write-Host "Clonando vcpkg..." -ForegroundColor Blue
        git clone https://github.com/Microsoft/vcpkg.git $VCPKG_ROOT
        & "$VCPKG_ROOT\bootstrap-vcpkg.bat"
    }
}

# Instalar dependências
Write-Host "Instalando dependências via vcpkg..." -ForegroundColor Blue
& "$VCPKG_ROOT\vcpkg" install nlohmann-json gtest --triplet x64-windows

# Criar diretórios de dados
Write-Host "Criando diretórios de dados..." -ForegroundColor Blue
New-Item -ItemType Directory -Force -Path "data\tables", "data\wal", "data\system" | Out-Null

# Build
Write-Host "Compilando OrangeSQL..." -ForegroundColor Blue
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Force -Path "build" | Out-Null
}
Set-Location build

cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
         -DVCPKG_TARGET_TRIPLET=x64-windows `
         -DCMAKE_BUILD_TYPE=Release

cmake --build . --config Release

Set-Location ..

Write-Host "✅ Build concluído!" -ForegroundColor Green
Write-Host ""
Write-Host "Para executar: .\build\bin\Release\orangesql.exe"