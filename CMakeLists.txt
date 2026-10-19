cmake_minimum_required(VERSION 3.22)

# Réglages du compilateur C
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Type de build par défaut
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Nom du projet
set(CMAKE_PROJECT_NAME smt32AI)

# Toolchain ARM
include("cmake/gcc-arm-none-eabi.cmake")

# Pour l'indexation (IntelliSense / clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})

# Sources générées par STM32CubeMX (HAL, main.c, startup...)
add_subdirectory(cmake/stm32cubemx)

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
)

# Fichiers du projet WWW
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/www_app.c
    Core/Src/rgb_led.c
    Core/Src/buttons.c
    Core/Src/dht11.c
    Core/Src/bh1750.c
    Core/Src/console.c
    Core/Src/settings.c
    Core/Src/rtc_clock.c
    Core/Src/gps.c
    Core/Src/sd_card.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Décommenter pour les tests rapides (10 s entre mesures) :
    # WWW_FAST_TEST=1
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)