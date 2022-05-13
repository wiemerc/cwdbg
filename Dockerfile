FROM debian:bookworm-slim

RUN dpkg --add-architecture i386 && apt update && apt install -y libc6:i386 make patch wget zip
COPY m68k-amigaos-toolchain-2.95.zip /
RUN unzip m68k-amigaos-toolchain-2.95.zip && rm m68k-amigaos-toolchain-2.95.zip

ENTRYPOINT ["/bin/sleep", "infinity"]