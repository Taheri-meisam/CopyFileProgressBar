

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <math.h>

void copyFile();


void copyFile() {
    std::string Destination;
    std::string Origin;
    std::string temp;
    std::string st;
    char* mem;
    double d_size = 0;
    double s_size = 0;
    double percent = 0;
    char c = NULL;
    std::cout << "Enter Origin file Name and address (use '\\\\' ):";
    std::cin >> Origin;
    std::fstream imageFile(Origin, std::ios::in | std::ios::binary);
    imageFile.seekg(0, std::ios::end);
    s_size = imageFile.tellg();
    std::cout << "Size : " << (s_size/1024) << "KB" << std::endl; // print size of the file
    imageFile.clear();
    imageFile.seekg(0, std::ios::beg);
    std::cout << "where do you want to copy the file (use '\\\\' ):";
    std::cin >> Destination;
    std::fstream CopyImageFile(Destination, std::ios::out | std::ios::binary);
    if (imageFile && CopyImageFile) {
        perror(" File exist");
        mem = new char[1024];
        const clock_t beginTime = clock();
        while (imageFile.read(mem, 1024)) {
            CopyImageFile.write(mem, 1024);
            d_size = CopyImageFile.tellg();
            percent = (d_size / s_size) * 100;

            if (percent <= 100)
            {

                std::cout << "\r" << "[" << std::string(percent / 2, (char)254u) << std::string(100 / 2 - percent / 2, ' ') << "]";
                std::cout << percent << " % " << " [ " << " of " << s_size << "]";
                std::cout.flush();
            }

        }
        const clock_t EndTime = clock();
        float min_t;
        float sec_t;
        float hour_t;
        sec_t = float(clock() - beginTime) / CLOCKS_PER_SEC;

        min_t = sec_t / 60;
        hour_t = min_t / 60;
        std::cout << std::setprecision(2) << "\n Copy finished in : " << (int)hour_t << ":" << (int)min_t << ":" << (int)sec_t;

    }
    else {
        perror("Image File");
    }


}
int main()
{
    copyFile();
}
