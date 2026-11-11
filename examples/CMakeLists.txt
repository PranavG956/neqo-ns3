build_lib_example(
  NAME neqo-link-example
  SOURCE_FILES neqo-link-example.cc
  LIBRARIES_TO_LINK ${libneqo} ${libcore}
)
build_lib_example(
  NAME neqo-basic
  SOURCE_FILES neqo-basic.cc
  LIBRARIES_TO_LINK
    ${libneqo}
    ${libinternet}
    ${libpoint-to-point}
    ${libapplications}
)

foreach(example neqo-dumbbell neqo-chain)
  build_lib_example(
    NAME ${example}
    SOURCE_FILES ${example}.cc
    LIBRARIES_TO_LINK
      ${libneqo}
      ${libinternet}
      ${libpoint-to-point}
      ${libapplications}
      ${libtraffic-control}
  )
endforeach()