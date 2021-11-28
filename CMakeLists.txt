
project(imxtrans)

include_directories(
    .
)

set(SOURCES 
    imxtrans.c    
)


add_executable(${PROJECT_NAME} ${SOURCES})



