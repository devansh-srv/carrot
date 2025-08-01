cmake_minimum_required(VERSION 3.10)
project(Carrot)

# Set the C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
# set(CMAKE_EXE_LINKER_FLAGS "-static")

# Set build directories
set(BUILD_DIR ${CMAKE_BINARY_DIR})

# Include directories (if you have headers in src/ or lib/)
include_directories(${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/lib)

# Automatically collect all C source files from src/
file(GLOB SOURCES "src/*.c")

# Add the client.c file separately if needed
# list(APPEND SOURCES client.c)

# Add the executable
add_executable(bin ${SOURCES})

#Add client exe
add_executable(client client.c)
