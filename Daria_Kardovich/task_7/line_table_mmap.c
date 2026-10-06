#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Хранит информацию об одной строке файла
struct Line {
    off_t position;  // Позиция начала строки
    size_t length;   // Длина строки
};


static volatile sig_atomic_t timed_out = 0;
void alarm_handler(int signal_number)
{
    (void)signal_number; // Убирает предупреждение о неиспользуемом параметре.
    timed_out = 1;       // Сообщаем основной программе: время вышло.
}

//Раньше функция принимала fd, использовала lseek() и read()
//Теперь файл уже находится в памяти, поэтому просто печатаем его. 
void print_file(const char *file_data, size_t file_size)
{
    fwrite(file_data, 1, file_size, stdout);
}

int main(int argc, char *argv[])
{
    const char *filename = "text.txt";
    int fd;

    //file_info хранит размер файла
    struct stat file_info;

    //file_data — адрес файла в памяти после mmap()
    char *file_data;

    struct Line *table = NULL;
    size_t count = 0;


    //вместо position и length при чтении через read() используем начало текущей строки
    size_t line_start = 0;

    int line_number;
    struct sigaction action;

    if (argc == 2) {
        filename = argv[1];
    }

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }


    //узнаём размер файла, чтобы отобразить его в память
    if (fstat(fd, &file_info) == -1) {
        perror("fstat");
        close(fd);
        return 1;
    }

    if (file_info.st_size == 0) {
        printf("The file is empty.\n");
        close(fd);
        return 0;
    }


    //mmap отображает весь файл в память.
    //После этого file_data[i] — i-й символ файла
    file_data = mmap(NULL, (size_t)file_info.st_size,
                     PROT_READ, MAP_PRIVATE, fd, 0);

    if (file_data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }


    //Раньше было read(fd, &symbol, 1)
    //Теперь идём по символам файла прямо в памяти
    for (size_t i = 0; i < (size_t)file_info.st_size; i++) {
        if (file_data[i] == '\n') {
            struct Line *new_table;

            new_table = realloc(
                table,
                (count + 1) * sizeof(struct Line)
            );

            if (new_table == NULL) {
                perror("realloc");
                free(table);

                // убираем файл из памяти
                munmap(file_data, (size_t)file_info.st_size);

                close(fd);
                return 1;
            }

            table = new_table;

            //начало строки уже хранится в line_start
            table[count].position = (off_t)line_start;
            table[count].length = i - line_start;

            count++;

            // Следующая строка начинается после '\n'
            line_start = i + 1;
        }
    }

    // Добавляем последнюю строку, если после неё нет '\n'
    if (line_start < (size_t)file_info.st_size) {
        struct Line *new_table;

        new_table = realloc(
            table,
            (count + 1) * sizeof(struct Line)
        );

        if (new_table == NULL) {
            perror("realloc");
            free(table);

            munmap(file_data, (size_t)file_info.st_size);

            close(fd);
            return 1;
        }

        table = new_table;
        table[count].position = (off_t)line_start;
        table[count].length = (size_t)file_info.st_size - line_start;
        count++;
    }

    action.sa_handler = alarm_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGALRM, &action, NULL) == -1) {
        perror("sigaction");
        free(table);

        munmap(file_data, (size_t)file_info.st_size);

        close(fd);
        return 1;
    }

    while (1) {
        int result;

        printf("Enter line number within 5 seconds: ");
        fflush(stdout);

        timed_out = 0;
        alarm(5);

        result = scanf("%d", &line_number);

        alarm(0);

        if (timed_out) {
            printf("\nTime is over. File contents:\n");


            //выводим файл из памяти, без lseek() и read()
            print_file(file_data, (size_t)file_info.st_size);
            free(table);
            munmap(file_data, (size_t)file_info.st_size);

            close(fd);
            return 0;
        }

        if (result != 1 || line_number < 0) {
            break;
        }

        if ((size_t)line_number >= count) {
            printf("There is no line with this number.\n");
            continue;
        }

           //Раньше здесь были lseek(), malloc(), read() и free()
           //Теперь строка уже находится в file_data
        printf("Selected line: %.*s\n",
               (int)table[line_number].length,
               file_data + table[line_number].position);
    }

    free(table);
    munmap(file_data, (size_t)file_info.st_size);

    close(fd);
    return 0;
}
