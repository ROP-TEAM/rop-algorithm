how to run 
-For the first time: 
    mkdir build
    cmake .. 
    make 
    ./solver

-Next time if you edit run this in build/:
    make 
    ./solver 

-Next timme if you add new file: 
Make sure you update CMakeLists.txt to include the new file first, then run in /build:
    cmake .. 
    make 
    ./solver
