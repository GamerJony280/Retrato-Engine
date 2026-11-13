#include <iostream>
#include <string>
int retrato_game_main(int argc,char **argv){
    if(argc!=2||std::string(argv[1])!=u8"archivo árbol-東京.glb"||argv[argc]!=nullptr){
        std::cerr<<"Entry must preserve UTF-8 arguments and trailing nullptr\n";return 1;
    }
    std::cout<<"PASS: Unicode entry arguments\n";return 0;
}
