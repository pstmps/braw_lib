%module bmd_metadata

%{
#include "bmd_metadata.cpp"
%}

%include "std_string.i"
%include "std_map.i"

%template(StringMap) std::map<std::string, std::string>;

extern std::map<std::string, std::string> read_metadata(const std::string filename);

// build with: 
// clang++ -Wno-everything -Wc++11-extensions -shared -fPIC bmd_metadata_wrap.cxx ./Include/BlackmagicRawAPIDispatch.cpp -I./Include -I./Libraries -I/Library/Frameworks/Python.framework/Versions/3.9/include/python3.9 -F./Libraries -framework BlackmagicRawAPI -framework CoreFoundation -L/Library/Frameworks/Python.framework/Versions/3.9/lib -lpython3.9 -lboost_filesystem -o _bmd_metadata.so