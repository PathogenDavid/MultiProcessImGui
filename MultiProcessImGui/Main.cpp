#include <iostream>
#include <Windows.h>

void ServerMain();
void ClientMain();

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        ClientMain();
    }
    else
    {
        ServerMain();
    }

    return 0;
}
