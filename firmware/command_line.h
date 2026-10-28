#pragma once
#include <stddef.h>
// Each transport owns its own parser: partial USB and UART lines cannot mix.
struct CommandLine {
 char bytes[96];size_t used=0;bool overflow=false;
 template<class Handler> void Feed(char c,Handler handler){
  if(c=='\n'){if(!overflow&&used){bytes[used]=0;handler(bytes);}used=0;overflow=false;}
  else if(c!='\r'){if(used<sizeof(bytes)-1)bytes[used++]=c;else overflow=true;}
 }
};
