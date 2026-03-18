    Name: Cynthia Brown 
    Date: 03/15/2026 
# CS4750 - Assignment 3

## Environment: 
    opsys server, Linux, gcc, Github Codespaces 
## How to compile the project: 
    Type 'make' (This will compile both the oss and worker executables using the all prefix ). 
## To clean up the executables and log files, type:
     'make clean'.
## Example of how to run the project: 
    ./oss -n 5 -s 2 -t 3.5 -i 0.5 -f log.txt 

## Version Control:
    I used git for version control and committed my changes periodically while working on the project. The .git subdirectory is included in my submission folder.

## Outstanding Problems:
    None. The simulation runs to completion, correctly bounds the child runtime between 1 and the -t parameter, limits the log file to 10,000 lines, properly recycles Process Control Block (PCB) entries, and cleans up all IPC shared memory and message queues upon natural termination or a Ctrl-C/60-second timeout.

## Problems Encountered:
    - Handling the fractional -t command line parameter and properly converting the fractional decimal remainder into nanoseconds without losing precision.

    - Ensuring the worker processes didn't get stuck in an infinite loop by caching the clock variable. I had to add the volatile keyword to the shared memory pointer.

    - Formatting the Process Table correctly so the columns aligned perfectly in both the terminal and the log file.

## Generative AI used AI Used: 
### GeminiPrompts: 
    - "This is what I have help me add the Fractional -t Generation Logic"

    - "identifier 'optarg' is undefined"
    
    - "Help me write the readme file"
### Summary: 
    I found the AI extremely helpful for troubleshooting specific C syntax and compiler errors. It helped me correctly extract the whole seconds and nanoseconds from the -t float parameter. It also helped me track down a bug where my worker.c needed the volatile keyword to properly read the shared memory clock updates. Overall, it was a great tool for catching small bugs and setting up the structure of the Makefile.