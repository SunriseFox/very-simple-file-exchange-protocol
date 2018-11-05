#include "consolereader.h"

ConsoleReader::ConsoleReader(Callback c): onCommandCallback(c), cin(stdin), cout(stdout)
{
    // ConsoleReader: 从控制台中读入指令并直接发送给主程序
}

void ConsoleReader::run() {
    while (!(command = cin.readLine()).isNull()) {
        onCommandCallback(command);
    }
}
