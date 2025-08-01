FROM gcc:bookworm AS builder
WORKDIR /app

COPY CMakeLists.txt ./
COPY client.c ./
COPY src/ ./src/
COPY lib/ ./lib/
COPY build.sh ./

RUN apt-get update && apt-get install -y cmake
RUN chmod +x build.sh
RUN ./build.sh

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y figlet
COPY --from=builder /app/build/bin /app/bin
COPY --from=builder /app/build/client /app/client
ENTRYPOINT ["/app/bin"]
