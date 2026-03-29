# Multi-stage build para OrangeSQL
FROM ubuntu:22.04 AS builder

# Instalar dependências de build
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    libreadline-dev \
    && rm -rf /var/lib/apt/lists/*

# Instalar vcpkg
WORKDIR /opt
RUN git clone https://github.com/Microsoft/vcpkg.git && \
    ./vcpkg/bootstrap-vcpkg.sh

# Copiar código fonte
WORKDIR /app
COPY . .

# Instalar dependências via vcpkg
RUN /opt/vcpkg/vcpkg install --triplet x64-linux

# Compilar
RUN mkdir build && cd build && \
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF && \
    make -j$(nproc)

# Stage final
FROM ubuntu:22.04

# Instalar runtime dependencies
RUN apt-get update && apt-get install -y \
    libreadline8 \
    && rm -rf /var/lib/apt/lists/*

# Criar usuário não-root
RUN useradd -m -s /bin/bash orangesql

# Copiar binário e dados
WORKDIR /app
COPY --from=builder /app/build/bin/orangesql /usr/local/bin/
COPY --from=builder /app/data /app/data

# Configurar diretórios
RUN mkdir -p /app/data/{tables,wal,system} && \
    chown -R orangesql:orangesql /app

# Expor porta (se usar modo servidor)
EXPOSE 5432

# Volume para dados persistentes
VOLUME ["/app/data"]

# Usuário não-root
USER orangesql

# Entrada
ENTRYPOINT ["orangesql"]
CMD ["--help"]