#!/bin/bash

# Script para configurar ambiente de desenvolvimento do OrangeSQL

set -e

echo "🍊 Configurando ambiente de desenvolvimento OrangeSQL..."

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

# Detectar sistema operacional
OS="$(uname -s)"
case "${OS}" in
    Linux*)     
        echo -e "${BLUE}📀 Detectado Linux${NC}"
        INSTALL_CMD="sudo apt-get install -y"
        PACKAGES="build-essential cmake git libreadline-dev libgtest-dev nlohmann-json3-dev"
        ;;
    Darwin*)    
        echo -e "${BLUE}📀 Detectado macOS${NC}"
        INSTALL_CMD="brew install"
        PACKAGES="cmake readline nlohmann-json googletest"
        ;;
    *)
        echo -e "${RED}❌ Sistema operacional não suportado: ${OS}${NC}"
        exit 1
        ;;
esac

# Função para verificar se comando existe
check_command() {
    if command -v $1 &> /dev/null; then
        echo -e "${GREEN}✅ $1 encontrado${NC}"
        return 0
    else
        echo -e "${RED}❌ $1 não encontrado${NC}"
        return 1
    fi
}

# Instalar dependências do sistema
echo -e "${BLUE}📦 Instalando dependências do sistema...${NC}"
if [ "${OS}" == "Linux" ]; then
    sudo apt-get update
    $INSTALL_CMD $PACKAGES
elif [ "${OS}" == "Darwin" ]; then
    if ! check_command brew; then
        echo -e "${RED}Homebrew não encontrado. Instalando...${NC}"
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    brew update
    $INSTALL_CMD $PACKAGES
fi

# Instalar/Atualizar vcpkg
if [ ! -d "vcpkg" ]; then
    echo -e "${BLUE}📦 Clonando vcpkg...${NC}"
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    cd ..
else
    echo -e "${GREEN}✅ vcpkg já existe${NC}"
    cd vcpkg
    git pull
    ./bootstrap-vcpkg.sh
    cd ..
fi

# Instalar dependências via vcpkg
echo -e "${BLUE}📦 Instalando dependências via vcpkg...${NC}"
./vcpkg/vcpkg install --triplet x64-linux

# Criar diretórios de dados
echo -e "${BLUE}📁 Criando diretórios de dados...${NC}"
mkdir -p data/{tables,wal,system,tmp}

# Configurar hooks do git
echo -e "${BLUE}🔧 Configurando git hooks...${NC}"
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
echo "🔍 Rodando testes antes do commit..."
make test || exit 1
echo "✅ Todos os testes passaram!"
EOF
chmod +x .git/hooks/pre-commit

# Criar arquivo de configuração
echo -e "${BLUE}⚙️ Criando arquivo de configuração...${NC}"
cat > orangesql.conf << EOF
# OrangeSQL Configuration File

[server]
port = 5432
host = localhost
max_connections = 100

[memory]
buffer_pool_size = 1024
max_sort_memory = 64

[logging]
log_level = INFO
log_file = data/wal/orangesql.log

[storage]
data_dir = data/tables
wal_dir = data/wal
EOF

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}✅ Ambiente configurado com sucesso!${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "Para compilar o OrangeSQL:"
echo "  mkdir build && cd build"
echo "  cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake"
echo "  make -j4"
echo ""
echo "Para rodar os testes:"
echo "  make test"
echo ""
echo "Para iniciar o OrangeSQL:"
echo "  ./bin/orangesql"