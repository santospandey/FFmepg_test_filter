echo -e "\t\e[34mcompiling....\e[0m"
if gcc -o main main_.c -lavfilter -lavcodec -lavformat -lavutil -lao; then
    echo -e "\t\e[32mcompiliation sucessfull\e[0m"
    echo -e "\n\t\e[35mProgram console now\e[0m"
    ./main ./samples/1.mp3
else
     echo -e "\t\e[31mcompiliation failed\e[0m"
fi

