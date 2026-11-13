#include "arguments.hpp"
#include <iostream>
int nova_main(int,char**);
int wmain(int argc,wchar_t **argv){try{
    retrato::detail::Arguments args(argc,argv);
    return nova_main(argc,args.data());
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
