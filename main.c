#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_MEMORY 60
#define MAX_PROCESSES 3

typedef struct {
    int processID;
    char *state;
    int priority;
    int programCounter;
    int lowerBound;
    int upperBound;
    int variableAddress;
} PCB;

typedef struct {
    char *name;
    char *value;
} memoryWord;

typedef struct {
    int front, rear, size;
    PCB *queue[MAX_PROCESSES];
} Queue;

memoryWord mainMemory[MAX_MEMORY];

bool userInputLock = false;
bool fileLock = false;
bool userOutputLock = false;

Queue readyQueue;
Queue userInputBlockedQueue;
Queue fileLockBlockedQueue;
Queue userOutputBlockedQueue;
Queue generalBlockedQueue;

int quantum;
int clockCycle = 0;

void initializeQueue(Queue *q) {
    q->front = 0;
    q->rear = -1;
    q->size = 0;
}

void enqueue(Queue *q, PCB *pcb) {
    if ((q->rear + 1) % MAX_PROCESSES == q->front && q->size != 0) {
        printf("Queue is full\n");
        return;
    }

    q->rear = (q->rear + 1) % MAX_PROCESSES;
    q->size++;
    q->queue[q->rear] = pcb;
}

PCB *dequeue(Queue *q) {
    PCB *pcb = NULL;
    if (q->size == 0) {
        printf("Queue is empty\n");
    } else {
        pcb = q->queue[q->front];
        q->front = (q->front + 1) % MAX_PROCESSES;
        q->size--;
    }
    return pcb;
}

bool isQueueEmpty(Queue *q) {
    return q->size == 0;
}

void initializeMemory() {
    for (int i = 0; i < MAX_MEMORY; i++) {
        memoryWord tmp = {"", ""};
        mainMemory[i] = tmp;
    }
}

char *intToChar(int number) {
    char *result = (char *)malloc(12 * sizeof(char));
    if (result == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }
    sprintf(result, "%d", number);
    return result;
}

int charToInt(const char *str) {
    if (str == NULL) {
        printf("Error: NULL input string\n");
        exit(1);
    }

    int result = atoi(str);

    if (result == 0 && strcmp(str, "0") != 0) {
        printf("Error: Invalid integer input\n");
        exit(1);
    }

    return result;
}

void promoteNewArrivalToFront() {
    Queue tempQueue;
    initializeQueue(&tempQueue);

    // Dequeue all elements except the last one and store them in the temporary queue
    while (!isQueueEmpty(&readyQueue) && (readyQueue.rear - readyQueue.front + MAX_PROCESSES) % MAX_PROCESSES > 1) {
        PCB* pcb = dequeue(&readyQueue);
        enqueue(&tempQueue, pcb);
    }

    // Dequeue the last element from the original queue
    PCB* lastElement = dequeue(&readyQueue);

    // Enqueue the last element back into the original queue
    enqueue(&readyQueue, lastElement);

    // Enqueue all elements from the temporary queue back into the original queue
    while (!isQueueEmpty(&tempQueue)) {
        PCB* element = dequeue(&tempQueue);
        enqueue(&readyQueue, element);
    }
}



void loadProgram(int processID, char *fileName) {
    FILE *file = fopen(fileName, "r");

    if (!file) {
        printf("Error opening file %s\n", fileName);
        return;
    }

    int lowerBound = processID * 20;
    int upperBound = ((processID + 1) * 20) - 1;
    int programCounter = lowerBound + 8;
    int variableAddress = lowerBound + 5;

    PCB *processPCB = (PCB *)malloc(sizeof(PCB));
    if (processPCB == NULL) {
        printf("Memory allocation failed\n");
        fclose(file);
        exit(1);
    }
    *processPCB = (PCB){processID, strdup("READY"), 1, programCounter, lowerBound, upperBound, variableAddress};

    memoryWord pid = {"Process ID", intToChar(processID)};
    memoryWord processState = {"State", "Ready"};
    memoryWord pc = {"PC", intToChar(programCounter)};
    memoryWord processLowerBound = {"Process Lower Bound", intToChar(lowerBound)};
    memoryWord processUpperBound = {"Process Upper Bound", intToChar(upperBound)};

    mainMemory[lowerBound] = pid;
    mainMemory[lowerBound + 1] = processState;
    mainMemory[lowerBound + 2] = pc;
    mainMemory[lowerBound + 3] = processLowerBound;
    mainMemory[lowerBound + 4] = processUpperBound;

    char line[256];
    int instructionCounter = 0;
    while (fgets(line, sizeof(line), file) != NULL && instructionCounter < 12) {
        char instructionName[50];
        sprintf(instructionName, "Instruction %d:", instructionCounter);
        memoryWord instruction = {strdup(instructionName), strdup(line)};
        mainMemory[programCounter + instructionCounter] = instruction;
        instructionCounter++;
    }

    fclose(file);

    enqueue(&readyQueue, processPCB);
    promoteNewArrivalToFront();
}

bool semWait(bool *mutex, Queue *blockedQueue, PCB *pcb) {
    if (*mutex) {
        printf("Process %d is blocked\n", pcb->processID);
        free(pcb->state);
        pcb->state = strdup("Blocked");
        memoryWord newState = {"State", strdup("Blocked")};
        mainMemory[pcb->lowerBound + 1] = newState;
        enqueue(blockedQueue, pcb);
        return true;
    } else {
        *mutex = true;
        return false;
    }
}

void semSignal(bool *mutex, Queue *blockedQueue) {
    if (!isQueueEmpty(blockedQueue)) {
        PCB *unblockedProcess = dequeue(blockedQueue);
        printf("Process %d is unblocked\n", unblockedProcess->processID);
        free(unblockedProcess->state);
        unblockedProcess->state = strdup("Ready");
        memoryWord newState = {"State", strdup("Ready")};
        mainMemory[unblockedProcess->lowerBound + 1] = newState;
        enqueue(&readyQueue, unblockedProcess);
    }else{
        *mutex = false;
    }
}

void incrementProgramCounter(PCB *pcb) {
    pcb->programCounter++;
    memoryWord newPC = {"PC", intToChar(pcb->programCounter)};
    mainMemory[pcb->lowerBound + 2] = newPC;
}

int search(char *param, int varAddress) {
    for (int i = varAddress; i < varAddress + 3; i++) {
        if (strcmp(mainMemory[i].name, param) == 0) {
            return i;
        }
    }
    return -1;
}

void executeInstruction(PCB *pcb) {
    int pc = pcb->programCounter;
    char *instructionToExecute = mainMemory[pcb->programCounter].value;

    char command[20];
    char firstParam[20];
    char secondParam[20];
    char thirdParam[20];

    sscanf(instructionToExecute, "%s %s %s %s", command, firstParam, secondParam, thirdParam);

    if (strcmp(command, "semWait") == 0) {
        if (strcmp(firstParam, "userInput") == 0) {
            if (semWait(&userInputLock, &userInputBlockedQueue, pcb)) {
                incrementProgramCounter(pcb);
                clockCycle++;
                return;
            }
        } else if (strcmp(firstParam, "file") == 0) {
            if (semWait(&fileLock, &fileLockBlockedQueue, pcb)) {
                incrementProgramCounter(pcb);
                clockCycle++;
                return;
            }
        } else if (strcmp(firstParam, "userOutput") == 0) {
            if (semWait(&userOutputLock, &userOutputBlockedQueue, pcb)) {
                incrementProgramCounter(pcb);
                clockCycle++;
                return;
            }
        }
    } else if (strcmp(command, "semSignal") == 0) {
        if (strcmp(firstParam, "userInput") == 0) {
            semSignal(&userInputLock, &userInputBlockedQueue);
        } else if (strcmp(firstParam, "file") == 0) {
            semSignal(&fileLock, &fileLockBlockedQueue);
        } else if (strcmp(firstParam, "userOutput") == 0) {
            semSignal(&userOutputLock, &userOutputBlockedQueue);
        }
    } else if (strcmp(command, "assign") == 0) {
        if (strcmp(secondParam, "input") == 0) {
            printf("Please enter a value: ");
            scanf("%s", secondParam);
            memoryWord newVar = {strdup(firstParam), strdup(secondParam)};
            mainMemory[pcb->variableAddress++] = newVar;
        } else if (strcmp(secondParam, "readFile") == 0) {
            FILE *file = fopen(mainMemory[search(thirdParam, pcb->lowerBound + 5)].value, "r");
            if (file) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), file) != NULL) {
                    char *newVar = buffer;
                }
                memoryWord newVar2 = {firstParam, strdup(buffer)};
                mainMemory[pcb->variableAddress++] = newVar2;
                fclose(file);
            }
        }
    } else if (strcmp(command, "printFromTo") == 0) {
        int start = search(firstParam, pcb->lowerBound + 5);
        int end = search(secondParam, pcb->lowerBound + 5);
        int start2 = charToInt(mainMemory[start].value);
        int end2 = charToInt(mainMemory[end].value);
        printf("From a to b : ");
        for (int i = start2; i <= end2; i++) {
            printf("%d ", i);
        }
        printf("\n");
    } else if (strcmp(command, "print") == 0) {
        int contentIdx = search(firstParam, pcb->lowerBound + 5);
        int content = charToInt(mainMemory[contentIdx].value);
        printf("Variable %s content : %d\n", firstParam, content);
    } else if (strcmp(command, "writeFile") == 0) {
        int fileNameIdx = search(firstParam, pcb->lowerBound + 5);
        int fileContentIdx = search(secondParam, pcb->lowerBound + 5);

        char *fileName = mainMemory[fileNameIdx].value;
        char *fileContent = mainMemory[fileContentIdx].value;

        FILE *file = fopen(fileName, "w");
        if (file) {
            fprintf(file, "%s", fileContent);
            fclose(file);
        }
    } else if (strcmp(command, "readFile") == 0) {
        FILE *file = fopen(mainMemory[search(firstParam, pcb->lowerBound + 5)].value, "r");
        if (file) {
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), file) != NULL) {
                printf("%s", buffer);
            }
            fclose(file);
        }
    }

    incrementProgramCounter(pcb);
    clockCycle++;
}

void runScheduler() {
    // Ensure that the scheduler runs if there are processes in the ready queue
    if (!isQueueEmpty(&readyQueue)) {
        PCB *currentProcess = dequeue(&readyQueue);
        int quantumCounter = 0;

        while (quantumCounter < quantum && strcmp(currentProcess->state, "Blocked") != 0 && strcmp(mainMemory[currentProcess->programCounter].value, "") != 0) {
            printf("-----------------------------------\n");
            printf("Clock Cycle : %d\n", clockCycle);
            printf("Process %d is currently executing. PC : %d\n", currentProcess->processID, currentProcess->programCounter);
            executeInstruction(currentProcess);
            quantumCounter++;
            printf("Process %d executed. New PC: %d\n", currentProcess->processID, currentProcess->programCounter);
        }

        if (strcmp(mainMemory[currentProcess->programCounter].value, "") == 0) {
            printf("Process %d completed. PC: %d\n", currentProcess->processID, currentProcess->programCounter);
            free(currentProcess->state);
            free(currentProcess);
        } else if (strcmp(currentProcess->state, "Blocked") != 0) {
            enqueue(&readyQueue, currentProcess);
        }
    }
}


int main() {
    printf("Enter the quantum for the scheduler: ");
    scanf("%d", &quantum);

    initializeQueue(&readyQueue);
    initializeQueue(&userInputBlockedQueue);
    initializeQueue(&fileLockBlockedQueue);
    initializeQueue(&userOutputBlockedQueue);
    initializeQueue(&generalBlockedQueue);

    initializeMemory();

    int arrivalTimes[MAX_PROCESSES];
    for (int i = 0; i < MAX_PROCESSES; i++) {
        printf("Enter arrival time for process %d: ", i);
        scanf("%d", &arrivalTimes[i]);
    }


    loadProgram(0, "Program_1.txt");

    int currentProcess = 1;
    bool running = true;

    while (running) {
        // Check for new process arrivals
        if (currentProcess < MAX_PROCESSES && clockCycle >= arrivalTimes[currentProcess]) {
            char fileName[20];
            sprintf(fileName, "Program_%d.txt", currentProcess + 1);
            loadProgram(currentProcess, fileName);
            currentProcess++;
        }

        // Run the scheduler
        runScheduler();

        // Check if all processes have been loaded and queues are empty to stop the loop
        if (currentProcess >= MAX_PROCESSES &&
            isQueueEmpty(&readyQueue) &&
            isQueueEmpty(&userInputBlockedQueue) &&
            isQueueEmpty(&fileLockBlockedQueue) &&
            isQueueEmpty(&userOutputBlockedQueue) &&
            isQueueEmpty(&generalBlockedQueue)) {
            running = false;
        }
    }

    return 0;
}

