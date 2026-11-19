include(FetchContent)
set(ORT_VERSION 1.23.2)
FetchContent_Declare(ort
  URL "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
  DOWNLOAD_NAME "onnxruntime-${ORT_VERSION}.zip"
  URL_HASH SHA256=25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ort)
add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${ort_SOURCE_DIR}/build/native/include")
if(WIN32)
  set(ORT_LIB_DIR "${ort_SOURCE_DIR}/runtimes/win-x64/native")
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_LIB_DIR}/onnxruntime.dll"
    IMPORTED_IMPLIB "${ORT_LIB_DIR}/onnxruntime.lib")
  install(FILES "${ORT_LIB_DIR}/onnxruntime.dll" DESTINATION bin)
elseif(APPLE)
  set(ORT_DYLIB "${ort_BINARY_DIR}/libonnxruntime.${ORT_VERSION}.dylib")
  file(MAKE_DIRECTORY "${ort_BINARY_DIR}")
  execute_process(COMMAND lipo -create
    "${ort_SOURCE_DIR}/runtimes/osx-arm64/native/libonnxruntime.dylib"
    "${ort_SOURCE_DIR}/runtimes/osx-x64/native/libonnxruntime.dylib"
    -output "${ORT_DYLIB}" COMMAND_ERROR_IS_FATAL ANY)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_LOCATION "${ORT_DYLIB}")
else()
  set(ORT_LIB_DIR "${ort_SOURCE_DIR}/runtimes/linux-x64/native")
  # NuGet uses an unversioned filename, but the binary declares this SONAME.
  file(CREATE_LINK libonnxruntime.so "${ORT_LIB_DIR}/libonnxruntime.so.1" SYMBOLIC)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_LIB_DIR}/libonnxruntime.so.1"
    IMPORTED_SONAME libonnxruntime.so.1)
endif()
install(FILES "${ort_SOURCE_DIR}/LICENSE" "${ort_SOURCE_DIR}/ThirdPartyNotices.txt" DESTINATION licenses/onnxruntime)
