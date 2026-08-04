get_filename_component(COMPONENT ${CMAKE_CURRENT_LIST_DIR} NAME)

include(EmbedBinaryFiles)
include(EmbedShaderFiles)
file(GLOB BINARY_FILES_TO_EMBED "${CMAKE_CURRENT_LIST_DIR}/src/fonts/*.ttf")
file(GLOB_RECURSE SHADER_FILES_TO_EMBED "${CMAKE_CURRENT_LIST_DIR}/shaders/*.glsl*")
embed_binary_files_rule(fonts.cpp ${BINARY_FILES_TO_EMBED})
embed_shader_files_rule(shaders.cpp "${SHADER_FILES_TO_EMBED}")

target_sources( ${COMPONENT}
PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/glchar.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gldraw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glfont.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gltext.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glpangoglu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gltexturecache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/viewport.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/opengl_render_state.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/stb_truetype.h
    ${CMAKE_CURRENT_BINARY_DIR}/fonts.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/shaders.cpp
)

find_package(Eigen3 REQUIRED NO_MODULE)
message(STATUS "Found Eigen: '${EIGEN3_INCLUDE_DIRS}'")
target_compile_definitions(${COMPONENT} PUBLIC HAVE_EIGEN)

set_target_properties(
    ${COMPONENT} PROPERTIES VERSION ${PANGOLIN_VERSION} SOVERSION ${PANGOLIN_VERSION_MAJOR}
)

target_link_libraries(${COMPONENT} PUBLIC pango_core pango_image Eigen3::Eigen)
target_include_directories(${COMPONENT} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include"
  DESTINATION ${CMAKE_INSTALL_PREFIX}
)

if(EMSCRIPTEN)
    target_compile_definitions(${COMPONENT} PUBLIC HAVE_GLES HAVE_GLES_2 HAVE_GLEW)
    target_sources( ${COMPONENT} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/compat/gl2engine.cpp)
else()
    if(_LINUX_)
        # EGL specific
        set(OpenGL_GL_PREFERENCE "GLVND")
        if(PANGOLIN_USE_GLES2)
            # OpenGL ES 2 path (e.g. Mali GPU on RK3588). Link libGLESv2 directly
            # and use <GLES2/gl2.h> instead of epoxy: <epoxy/gl.h> would declare
            # desktop fixed-function symbols (glColor4f, ...) that clash with the
            # inline emulations in compat/gl2engine.h. Same approach as Emscripten.
            find_package(OpenGL REQUIRED COMPONENTS EGL)
            find_library(GLES2_LIBRARY NAMES GLESv2 REQUIRED)
            find_path(GLES2_INCLUDE_DIR GLES2/gl2.h)
            target_include_directories(${COMPONENT} PUBLIC $<BUILD_INTERFACE:${GLES2_INCLUDE_DIR}>)
            target_link_libraries(${COMPONENT} PUBLIC OpenGL::EGL ${GLES2_LIBRARY})
            target_compile_definitions(${COMPONENT} PUBLIC HAVE_GLES HAVE_GLES_2)
            target_sources( ${COMPONENT} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/compat/gl2engine.cpp)
        else()
            find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
            find_package(epoxy REQUIRED)
            target_link_libraries(${COMPONENT} PUBLIC OpenGL::OpenGL OpenGL::EGL)
            target_link_libraries(${COMPONENT} PUBLIC ${epoxy_LIBRARIES})
            target_include_directories(${COMPONENT} PUBLIC $<BUILD_INTERFACE:${epoxy_INCLUDE_DIRS}>)
            target_compile_definitions(${COMPONENT} PUBLIC HAVE_EPOXY)
        endif()
    else()
        # OpenGL defaults
        find_package(OpenGL REQUIRED)
        find_package(GLEW REQUIRED)
        target_include_directories(${COMPONENT} PUBLIC $<BUILD_INTERFACE:${GLEW_INCLUDE_DIR}>)
        target_link_libraries(${COMPONENT} PUBLIC ${GLEW_LIBRARY})
        target_link_libraries(${COMPONENT} PUBLIC ${OPENGL_LIBRARIES})
        target_include_directories(${COMPONENT} PUBLIC $<BUILD_INTERFACE:${OPENGL_INCLUDE_DIR}>)
        target_compile_definitions(${COMPONENT} PUBLIC HAVE_GLEW)
    endif()
endif()
