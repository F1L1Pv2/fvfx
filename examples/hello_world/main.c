#include <stdio.h>

#include "engine/app.h"

int main(){
    printf("Hello world\n");

    return 0;
}

bool afterResize(){
    return false;
}
bool update(float deltaTime){
    return false;
}
bool draw(){
    return false;
}