#include <stdio.h>    // ctrl+shift+"~"
#include <stdlib.h>   //cd "OneDrive\Desktop\ayuC"
#include <time.h>     //gcc ds_mp.c -o hospital_app.exe -I"C:\c pro\mysql-connector-c-6.1.11-win32\include" -L"C:\c pro\mysql-connector-c-6.1.11-win32\lib" -llibmysql
#include <string.h>   //.\hospital_app.exe 
#include "mysql.h"

/* ===================== CONFIGURATION ===================== */
#define DB_HOST     "localhost"
#define DB_USER     "root"
#define DB_PASS     ""   //enter your mysql password here in the blank space
#define DB_NAME     "hospital_management"
#define DB_PORT     3306

/* ===================== STRUCTURES ===================== */
typedef struct Patient {
    int patientID;
    char name[100];
    int age;
    char gender[20];
    char contact[20];
    char symptoms[200];
    int emergencyLevel;          /* 1=Critical ... 4=Minor */
    char registrationTime[30];
    char status[30];             /* WAITING, IN TREATMENT, ADMITTED, DISCHARGED */
} Patient;

typedef struct Node {
    Patient data;
    struct Node *next;
    struct Node *prev;
} Node;

typedef struct QueueNode {
    Patient data;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode *front;
    QueueNode *rear;
} Queue;

/* Global handles */
Node *head = NULL;               /* Linked list of all patients */
Queue normalQ;                   /* Level 3 & 4 WAITING */
Queue priorityQ;                 /* Level 1 & 2 WAITING (kept sorted) */
MYSQL *conn = NULL;

/* ===================== UTILITY ===================== */
void clearInputBuffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void safeInput(char *buf, int size) {
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
}

void getCurrentTime(char *buf, int size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

int isValidStatus(const char *s) {
    return (strcmp(s, "WAITING") == 0 ||
            strcmp(s, "IN TREATMENT") == 0 ||
            strcmp(s, "ADMITTED") == 0 ||
            strcmp(s, "DISCHARGED") == 0);
}

/* ===================== MYSQL HELPERS ===================== */
int dbConnect(void) {
    conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return 0;
    }


    enum mysql_ssl_mode mode = SSL_MODE_DISABLED;
    mysql_options(conn, MYSQL_OPT_SSL_MODE, &mode);

    if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "Connection failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        conn = NULL;
        return 0;
    }
    printf("Connected to MySQL successfully.\n");
    return 1;
}
    
void dbClose(void) {
    if (conn) {
        mysql_close(conn);
        conn = NULL;
    }
}

/* Escape and execute a simple query; returns 1 on success */
int dbExecute(const char *query) {
    if (mysql_query(conn, query)) {
        fprintf(stderr, "SQL Error: %s\nQuery: %s\n", mysql_error(conn), query);
        return 0;
    }
    return 1;
}

/* Add a history entry */
void addHistory(int pid, const char *action, const char *desc) {
    char q[600];
    char timeBuf[30];
    getCurrentTime(timeBuf, sizeof(timeBuf));
    /* Simple escaping for demo – production would use prepared statements */
    char safeDesc[256];
    mysql_real_escape_string(conn, safeDesc, desc, strlen(desc));
    snprintf(q, sizeof(q),
             "INSERT INTO medical_history (patient_id, action, description, action_time) "
             "VALUES (%d, '%s', '%s', '%s')",
             pid, action, safeDesc, timeBuf);
    dbExecute(q);
}

/* ===================== LINKED LIST ===================== */
Node *createNode(Patient p) {
    Node *n = (Node *)malloc(sizeof(Node));
    if (!n) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }
    n->data = p;
    n->next = n->prev = NULL;
    return n;
}

void insertPatientList(Patient p) {
    Node *n = createNode(p);
    if (head == NULL) {
        head = n;
        return;
    }
    Node *cur = head;
    while (cur->next) cur = cur->next;
    cur->next = n;
    n->prev = cur;
}

Node *findByID(int id) {
    Node *cur = head;
    while (cur) {
        if (cur->data.patientID == id) return cur;
        cur = cur->next;
    }
    return NULL;
}

void deleteFromList(int id) {
    Node *n = findByID(id);
    if (!n) return;
    if (n->prev) n->prev->next = n->next;
    else head = n->next;
    if (n->next) n->next->prev = n->prev;
    free(n);
}

void freeEntireList(void) {
    Node *cur = head;
    while (cur) {
        Node *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    head = NULL;
}

void displayAllPatients(void) {
    if (!head) {
        printf("\nNo patients in the system.\n");
        return;
    }
    printf("\n%-6s %-20s %-4s %-8s %-15s %-8s %-12s %-20s\n",
           "ID", "Name", "Age", "Gender", "Contact", "Level", "Status", "Reg. Time");
    printf("---------------------------------------------------------------------------------------------\n");
    Node *cur = head;
    while (cur) {
        printf("%-6d %-20s %-4d %-8s %-15s %-8d %-12s %-20s\n",
               cur->data.patientID, cur->data.name, cur->data.age,
               cur->data.gender, cur->data.contact,
               cur->data.emergencyLevel, cur->data.status,
               cur->data.registrationTime);
        cur = cur->next;
    }
}

/* ===================== NORMAL QUEUE (FIFO) ===================== */
void initQueue(Queue *q) {
    q->front = q->rear = NULL;
}

int isEmptyQ(Queue *q) {
    return q->front == NULL;
}

void enqueue(Queue *q, Patient p) {
    QueueNode *n = (QueueNode *)malloc(sizeof(QueueNode));
    if (!n) { fprintf(stderr, "Queue malloc failed\n"); exit(1); }
    n->data = p;
    n->next = NULL;
    if (q->rear == NULL) {
        q->front = q->rear = n;
    } else {
        q->rear->next = n;
        q->rear = n;
    }
}

int dequeue(Queue *q, Patient *out) {
    if (isEmptyQ(q)) return 0;
    QueueNode *tmp = q->front;
    *out = tmp->data;
    q->front = q->front->next;
    if (q->front == NULL) q->rear = NULL;
    free(tmp);
    return 1;
}

void displayQueue(Queue *q, const char *title) {
    printf("\n=== %s ===\n", title);
    if (isEmptyQ(q)) {
        printf("(empty)\n");
        return;
    }
    QueueNode *cur = q->front;
    while (cur) {
        printf("ID:%d  %s  Level:%d  Status:%s\n",
               cur->data.patientID, cur->data.name,
               cur->data.emergencyLevel, cur->data.status);
        cur = cur->next;
    }
}

void freeQueue(Queue *q) {
    Patient dummy;
    while (dequeue(q, &dummy));
}

/* ===================== PRIORITY QUEUE (stable) ===================== */
/* Insert keeping order: lower emergencyLevel first; same level → FIFO */
void insertEmergencyPatient(Patient p) {
    QueueNode *n = (QueueNode *)malloc(sizeof(QueueNode));
    if (!n) { fprintf(stderr, "Priority queue malloc failed\n"); exit(1); }
    n->data = p;
    n->next = NULL;

    if (priorityQ.front == NULL) {
        priorityQ.front = priorityQ.rear = n;
        return;
    }

    /* Find insertion point */
    QueueNode *prev = NULL;
    QueueNode *cur = priorityQ.front;
    while (cur && cur->data.emergencyLevel <= p.emergencyLevel) {
        /* same level: keep existing (arrival order) */
        prev = cur;
        cur = cur->next;
    }
    if (prev == NULL) {          /* insert at front */
        n->next = priorityQ.front;
        priorityQ.front = n;
    } else {
        n->next = prev->next;
        prev->next = n;
        if (n->next == NULL) priorityQ.rear = n;
    }
}

int removeHighestPriorityPatient(Patient *out) {
    if (isEmptyQ(&priorityQ)) return 0;
    QueueNode *tmp = priorityQ.front;
    *out = tmp->data;
    priorityQ.front = priorityQ.front->next;
    if (priorityQ.front == NULL) priorityQ.rear = NULL;
    free(tmp);
    return 1;
}

void displayEmergencyQueue(void) {
    displayQueue(&priorityQ, "EMERGENCY PRIORITY QUEUE (Level 1 first)");
}

/* Remove a patient from either queue by ID (used on discharge / status change) */
void removeFromQueues(int id) {
    /* Normal queue */
    QueueNode *prev = NULL, *cur = normalQ.front;
    while (cur) {
        if (cur->data.patientID == id) {
            if (prev) prev->next = cur->next;
            else normalQ.front = cur->next;
            if (cur == normalQ.rear) normalQ.rear = prev;
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    /* Priority queue */
    prev = NULL; cur = priorityQ.front;
    while (cur) {
        if (cur->data.patientID == id) {
            if (prev) prev->next = cur->next;
            else priorityQ.front = cur->next;
            if (cur == priorityQ.rear) priorityQ.rear = prev;
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
}

/* ===================== LOAD FROM DATABASE ===================== */
void loadPatientsFromDB(void) {
    if (mysql_query(conn, "SELECT patient_id, name, age, gender, contact, symptoms, "
                          "emergency_level, registration_time, status FROM patients")) {
        fprintf(stderr, "Load failed: %s\n", mysql_error(conn));
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Patient p;
        p.patientID = atoi(row[0]);
        strncpy(p.name, row[1] ? row[1] : "", sizeof(p.name)-1);
        p.age = atoi(row[2]);
        strncpy(p.gender, row[3] ? row[3] : "", sizeof(p.gender)-1);
        strncpy(p.contact, row[4] ? row[4] : "", sizeof(p.contact)-1);
        strncpy(p.symptoms, row[5] ? row[5] : "", sizeof(p.symptoms)-1);
        p.emergencyLevel = atoi(row[6]);
        strncpy(p.registrationTime, row[7] ? row[7] : "", sizeof(p.registrationTime)-1);
        strncpy(p.status, row[8] ? row[8] : "", sizeof(p.status)-1);

        insertPatientList(p);

        /* Rebuild queues only for WAITING patients */
        if (strcmp(p.status, "WAITING") == 0) {
            if (p.emergencyLevel <= 2)
                insertEmergencyPatient(p);
            else
                enqueue(&normalQ, p);
        }
    }
    mysql_free_result(res);
    printf("Loaded patients from database into memory.\n");
}

/* ===================== REGISTER ===================== */
int generateNextID(void) {
    int maxID = 100;   /* start from 101 */
    Node *cur = head;
    while (cur) {
        if (cur->data.patientID > maxID) maxID = cur->data.patientID;
        cur = cur->next;
    }
    return maxID + 1;
}

void registerPatient(void) {
    Patient p;
    char buf[256];

    p.patientID = generateNextID();
    printf("\nGenerated Patient ID: %d\n", p.patientID);

    printf("Name: ");
    safeInput(p.name, sizeof(p.name));
    if (strlen(p.name) == 0) { printf("Name cannot be empty.\n"); return; }

    printf("Age: ");
    safeInput(buf, sizeof(buf));
    p.age = atoi(buf);
    if (p.age <= 0 || p.age > 150) { printf("Invalid age.\n"); return; }

    printf("Gender (M/F/Other): ");
    safeInput(p.gender, sizeof(p.gender));
    if (strlen(p.gender) == 0) { printf("Gender cannot be empty.\n"); return; }

    printf("Contact Number: ");
    safeInput(p.contact, sizeof(p.contact));

    printf("Symptoms / Disease: ");
    safeInput(p.symptoms, sizeof(p.symptoms));

    printf("Emergency Level (1=Critical, 2=Serious, 3=Moderate, 4=Minor): ");
    safeInput(buf, sizeof(buf));
    p.emergencyLevel = atoi(buf);
    if (p.emergencyLevel < 1 || p.emergencyLevel > 4) {
        printf("Invalid emergency level.\n");
        return;
    }

    getCurrentTime(p.registrationTime, sizeof(p.registrationTime));
    strcpy(p.status, "WAITING");

    /* Insert into MySQL first */
    char q[800];
    char escName[200], escGender[40], escContact[40], escSym[400];
    mysql_real_escape_string(conn, escName, p.name, strlen(p.name));
    mysql_real_escape_string(conn, escGender, p.gender, strlen(p.gender));
    mysql_real_escape_string(conn, escContact, p.contact, strlen(p.contact));
    mysql_real_escape_string(conn, escSym, p.symptoms, strlen(p.symptoms));

    snprintf(q, sizeof(q),
             "INSERT INTO patients VALUES (%d,'%s',%d,'%s','%s','%s',%d,'%s','%s')",
             p.patientID, escName, p.age, escGender, escContact, escSym,
             p.emergencyLevel, p.registrationTime, p.status);

    if (!dbExecute(q)) {
        printf("Failed to save patient to database. Registration aborted.\n");
        return;
    }

    /* Now update memory structures */
    insertPatientList(p);
    if (p.emergencyLevel <= 2)
        insertEmergencyPatient(p);
    else
        enqueue(&normalQ, p);

    addHistory(p.patientID, "Patient Registered", "New patient registered and placed in waiting queue");

    printf("\nPatient registered successfully. ID = %d\n", p.patientID);
}

/* ===================== SEARCH ===================== */
void searchPatient(void) {
    int choice;
    char buf[100];
    printf("\nSearch by:\n1. Patient ID\n2. Name\n3. Contact\nChoice: ");
    safeInput(buf, sizeof(buf));
    choice = atoi(buf);

    Node *cur = head;
    int found = 0;

    if (choice == 1) {
        printf("Enter Patient ID: ");
        safeInput(buf, sizeof(buf));
        int id = atoi(buf);
        while (cur) {
            if (cur->data.patientID == id) {
                printf("\n*** Patient Found ***\n");
                printf("ID: %d\nName: %s\nAge: %d\nGender: %s\nContact: %s\n"
                       "Symptoms: %s\nEmergency Level: %d\nStatus: %s\nReg Time: %s\n",
                       cur->data.patientID, cur->data.name, cur->data.age,
                       cur->data.gender, cur->data.contact, cur->data.symptoms,
                       cur->data.emergencyLevel, cur->data.status, cur->data.registrationTime);
                found = 1;
                break;
            }
            cur = cur->next;
        }
    } else if (choice == 2) {
        printf("Enter Name (partial ok): ");
        safeInput(buf, sizeof(buf));
        while (cur) {
            if (strstr(cur->data.name, buf) != NULL) {
                printf("\nID:%d  %s  Age:%d  Level:%d  Status:%s\n",
                       cur->data.patientID, cur->data.name,
                       cur->data.age, cur->data.emergencyLevel, cur->data.status);
                found = 1;
            }
            cur = cur->next;
        }
    } else if (choice == 3) {
        printf("Enter Contact: ");
        safeInput(buf, sizeof(buf));
        while (cur) {
            if (strcmp(cur->data.contact, buf) == 0) {
                printf("\n*** Patient Found ***\n");
                printf("ID: %d  Name: %s  Status: %s\n",
                       cur->data.patientID, cur->data.name, cur->data.status);
                found = 1;
            }
            cur = cur->next;
        }
    }

    if (!found) printf("\nPatient Not Found\n");
}

/* ===================== UPDATE ===================== */
void updatePatient(void) {
    char buf[100];
    printf("Enter Patient ID to update: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);
    Node *n = findByID(id);
    if (!n) { printf("Patient not found.\n"); return; }
    if (strcmp(n->data.status, "DISCHARGED") == 0) {
        printf("Cannot update a discharged patient.\n");
        return;
    }

    printf("Current name: %s\nNew name (Enter to keep): ", n->data.name);
    safeInput(buf, sizeof(buf));
    if (strlen(buf) > 0) strncpy(n->data.name, buf, sizeof(n->data.name)-1);

    printf("Current age: %d\nNew age (0 to keep): ", n->data.age);
    safeInput(buf, sizeof(buf));
    int a = atoi(buf);
    if (a > 0) n->data.age = a;

    printf("Current contact: %s\nNew contact (Enter to keep): ", n->data.contact);
    safeInput(buf, sizeof(buf));
    if (strlen(buf) > 0) strncpy(n->data.contact, buf, sizeof(n->data.contact)-1);

    printf("Current symptoms: %s\nNew symptoms (Enter to keep): ", n->data.symptoms);
    safeInput(buf, sizeof(buf));
    if (strlen(buf) > 0) strncpy(n->data.symptoms, buf, sizeof(n->data.symptoms)-1);

    /* Update DB */
    char q[800];
    char escName[200], escContact[40], escSym[400];
    mysql_real_escape_string(conn, escName, n->data.name, strlen(n->data.name));
    mysql_real_escape_string(conn, escContact, n->data.contact, strlen(n->data.contact));
    mysql_real_escape_string(conn, escSym, n->data.symptoms, strlen(n->data.symptoms));

    snprintf(q, sizeof(q),
             "UPDATE patients SET name='%s', age=%d, contact='%s', symptoms='%s' WHERE patient_id=%d",
             escName, n->data.age, escContact, escSym, id);
    if (dbExecute(q)) {
        addHistory(id, "Patient Information Updated", "Details updated by staff");
        printf("Patient updated successfully.\n");
    }
}

/* ===================== DELETE ===================== */
void deletePatient(void) {
    char buf[50];
    printf("Enter Patient ID to permanently delete: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);
    Node *n = findByID(id);
    if (!n) { printf("Patient not found.\n"); return; }

    printf("Are you sure? This removes the patient and history (y/n): ");
    safeInput(buf, sizeof(buf));
    if (buf[0] != 'y' && buf[0] != 'Y') return;

    char q[100];
    snprintf(q, sizeof(q), "DELETE FROM patients WHERE patient_id=%d", id);
    if (dbExecute(q)) {
        removeFromQueues(id);
        deleteFromList(id);
        printf("Patient deleted.\n");
    }
}

/* ===================== STATUS CHANGE ===================== */
void changeStatus(void) {
    char buf[50];
    printf("Enter Patient ID: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);
    Node *n = findByID(id);
    if (!n) { printf("Patient not found.\n"); return; }

    printf("Current status: %s\n", n->data.status);
    printf("New status (WAITING / IN TREATMENT / ADMITTED / DISCHARGED): ");
    safeInput(buf, sizeof(buf));
    if (!isValidStatus(buf)) { printf("Invalid status.\n"); return; }

    /* Basic transition rules */
    if (strcmp(n->data.status, "DISCHARGED") == 0) {
        printf("Cannot change status of already discharged patient.\n");
        return;
    }
    if (strcmp(buf, "WAITING") == 0 && strcmp(n->data.status, "DISCHARGED") != 0) {
        /* allow only if coming back somehow – keep simple */
    }

    strcpy(n->data.status, buf);

    char q[200];
    snprintf(q, sizeof(q), "UPDATE patients SET status='%s' WHERE patient_id=%d", buf, id);
    if (dbExecute(q)) {
        addHistory(id, "Status Changed", buf);
        /* If moved out of WAITING, remove from queues */
        if (strcmp(buf, "WAITING") != 0)
            removeFromQueues(id);
        printf("Status updated.\n");
    }
}

/* ===================== DISCHARGE ===================== */
void dischargePatient(void) {
    char buf[50];
    printf("Enter Patient ID to discharge: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);
    Node *n = findByID(id);
    if (!n) { printf("Patient not found.\n"); return; }
    if (strcmp(n->data.status, "DISCHARGED") == 0) {
        printf("Patient is already discharged.\n");
        return;
    }

    strcpy(n->data.status, "DISCHARGED");
    removeFromQueues(id);

    char q[200];
    snprintf(q, sizeof(q), "UPDATE patients SET status='DISCHARGED' WHERE patient_id=%d", id);
    if (dbExecute(q)) {
        addHistory(id, "Patient Discharged", "Patient discharged from hospital");
        printf("Patient discharged successfully.\n");
    }
}

/* ===================== TREAT NEXT ===================== */
void treatNextPatient(void) {
    Patient p;
    int got = 0;

    /* Priority queue first */
    if (!isEmptyQ(&priorityQ)) {
        got = removeHighestPriorityPatient(&p);
    } else if (!isEmptyQ(&normalQ)) {
        got = dequeue(&normalQ, &p);
    }

    if (!got) {
        printf("No patients waiting for treatment.\n");
        return;
    }

    Node *n = findByID(p.patientID);
    if (!n || strcmp(n->data.status, "WAITING") != 0) {
        printf("Patient no longer waiting. Skipping.\n");
        return;
    }

    strcpy(n->data.status, "IN TREATMENT");
    char q[200];
    snprintf(q, sizeof(q), "UPDATE patients SET status='IN TREATMENT' WHERE patient_id=%d", p.patientID);
    if (dbExecute(q)) {
        addHistory(p.patientID, "Treatment Started", "Patient taken for treatment");
        printf("\n*** Now treating ***\n");
        printf("ID: %d  Name: %s  Level: %d\n", p.patientID, p.name, p.emergencyLevel);
    }
}

/* ===================== FILTER ===================== */
void filterPatients(void) {
    char buf[50];
    printf("\nFilter by:\n1. Emergency Level\n2. Status\n3. Age range\n4. Gender\nChoice: ");
    safeInput(buf, sizeof(buf));
    int ch = atoi(buf);

    Node *cur = head;
    int count = 0;

    if (ch == 1) {
        printf("Enter level (1-4): ");
        safeInput(buf, sizeof(buf));
        int lvl = atoi(buf);
        while (cur) {
            if (cur->data.emergencyLevel == lvl) {
                printf("ID:%d %s Level:%d Status:%s\n",
                       cur->data.patientID, cur->data.name,
                       cur->data.emergencyLevel, cur->data.status);
                count++;
            }
            cur = cur->next;
        }
    } else if (ch == 2) {
        printf("Enter status: ");
        safeInput(buf, sizeof(buf));
        while (cur) {
            if (strcmp(cur->data.status, buf) == 0) {
                printf("ID:%d %s Status:%s\n",
                       cur->data.patientID, cur->data.name, cur->data.status);
                count++;
            }
            cur = cur->next;
        }
    } else if (ch == 3) {
        printf("Min age: "); safeInput(buf, sizeof(buf)); int minA = atoi(buf);
        printf("Max age: "); safeInput(buf, sizeof(buf)); int maxA = atoi(buf);
        while (cur) {
            if (cur->data.age >= minA && cur->data.age <= maxA) {
                printf("ID:%d %s Age:%d\n",
                       cur->data.patientID, cur->data.name, cur->data.age);
                count++;
            }
            cur = cur->next;
        }
    } else if (ch == 4) {
        printf("Gender: ");
        safeInput(buf, sizeof(buf));
        while (cur) {
            if (strcasecmp(cur->data.gender, buf) == 0) {
                printf("ID:%d %s Gender:%s\n",
                       cur->data.patientID, cur->data.name, cur->data.gender);
                count++;
            }
            cur = cur->next;
        }
    }
    printf("Total matching: %d\n", count);
}

/* ===================== SORTING ===================== */
/* Bubble Sort by chosen field – works on a temporary array of pointers */
void sortPatients(void) {
    if (!head) { printf("No patients.\n"); return; }

    /* Count nodes */
    int n = 0;
    Node *cur = head;
    while (cur) { n++; cur = cur->next; }

    Node **arr = (Node **)malloc(n * sizeof(Node *));
    if (!arr) return;
    cur = head;
    for (int i = 0; i < n; i++) { arr[i] = cur; cur = cur->next; }

    char buf[20];
    printf("Sort by:\n1. Patient ID\n2. Name\n3. Age\n4. Emergency Level\nChoice: ");
    safeInput(buf, sizeof(buf));
    int field = atoi(buf);

    printf("Algorithm: 1=Bubble Sort  2=Selection Sort\nChoice: ");
    safeInput(buf, sizeof(buf));
    int algo = atoi(buf);

    if (algo == 1) {   /* Bubble Sort */
        for (int i = 0; i < n-1; i++) {
            for (int j = 0; j < n-i-1; j++) {
                int swap = 0;
                if (field == 1 && arr[j]->data.patientID > arr[j+1]->data.patientID) swap = 1;
                else if (field == 2 && strcmp(arr[j]->data.name, arr[j+1]->data.name) > 0) swap = 1;
                else if (field == 3 && arr[j]->data.age > arr[j+1]->data.age) swap = 1;
                else if (field == 4 && arr[j]->data.emergencyLevel > arr[j+1]->data.emergencyLevel) swap = 1;
                if (swap) {
                    Node *tmp = arr[j];
                    arr[j] = arr[j+1];
                    arr[j+1] = tmp;
                }
            }
        }
        printf("Sorted using Bubble Sort.\n");
    } else {           /* Selection Sort */
        for (int i = 0; i < n-1; i++) {
            int minIdx = i;
            for (int j = i+1; j < n; j++) {
                int better = 0;
                if (field == 1 && arr[j]->data.patientID < arr[minIdx]->data.patientID) better = 1;
                else if (field == 2 && strcmp(arr[j]->data.name, arr[minIdx]->data.name) < 0) better = 1;
                else if (field == 3 && arr[j]->data.age < arr[minIdx]->data.age) better = 1;
                else if (field == 4 && arr[j]->data.emergencyLevel < arr[minIdx]->data.emergencyLevel) better = 1;
                if (better) minIdx = j;
            }
            if (minIdx != i) {
                Node *tmp = arr[i];
                arr[i] = arr[minIdx];
                arr[minIdx] = tmp;
            }
        }
        printf("Sorted using Selection Sort.\n");
    }

    /* Display sorted view (does NOT change permanent order or DB) */
    printf("\n%-6s %-20s %-4s %-8s %-12s\n", "ID", "Name", "Age", "Level", "Status");
    for (int i = 0; i < n; i++) {
        printf("%-6d %-20s %-4d %-8d %-12s\n",
               arr[i]->data.patientID, arr[i]->data.name,
               arr[i]->data.age, arr[i]->data.emergencyLevel, arr[i]->data.status);
    }
    free(arr);
}

/* ===================== HISTORY ===================== */
void viewHistory(void) {
    char buf[50];
    printf("Enter Patient ID: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);

    char q[200];
    snprintf(q, sizeof(q),
             "SELECT action, description, action_time FROM medical_history "
             "WHERE patient_id=%d ORDER BY action_time", id);
    if (mysql_query(conn, q)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        printf("No history found.\n");
        if (res) mysql_free_result(res);
        return;
    }
    printf("\n--- Medical History for Patient %d ---\n", id);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        printf("[%s] %s - %s\n", row[2], row[0], row[1] ? row[1] : "");
    }
    mysql_free_result(res);
}

void addHistoryManual(void) {
    char buf[100], desc[200];
    printf("Patient ID: ");
    safeInput(buf, sizeof(buf));
    int id = atoi(buf);
    if (!findByID(id)) { printf("Patient not found.\n"); return; }
    printf("Action description: ");
    safeInput(desc, sizeof(desc));
    addHistory(id, "Manual Entry", desc);
    printf("History entry added.\n");
}

/* ===================== STATISTICS ===================== */
void displayStatistics(void) {
    int total = 0, waiting = 0, treatment = 0, admitted = 0, discharged = 0;
    int crit = 0, ser = 0, mod = 0, minr = 0;

    Node *cur = head;
    while (cur) {
        total++;
        if (strcmp(cur->data.status, "WAITING") == 0) waiting++;
        else if (strcmp(cur->data.status, "IN TREATMENT") == 0) treatment++;
        else if (strcmp(cur->data.status, "ADMITTED") == 0) admitted++;
        else if (strcmp(cur->data.status, "DISCHARGED") == 0) discharged++;

        if (cur->data.emergencyLevel == 1) crit++;
        else if (cur->data.emergencyLevel == 2) ser++;
        else if (cur->data.emergencyLevel == 3) mod++;
        else if (cur->data.emergencyLevel == 4) minr++;
        cur = cur->next;
    }

    printf("\n========== STATISTICS ==========\n");
    printf("Total Patients      : %d\n", total);
    printf("Waiting             : %d\n", waiting);
    printf("In Treatment        : %d\n", treatment);
    printf("Admitted            : %d\n", admitted);
    printf("Discharged          : %d\n", discharged);
    printf("--------------------------------\n");
    printf("Critical (Level 1)  : %d\n", crit);
    printf("Serious  (Level 2)  : %d\n", ser);
    printf("Moderate (Level 3)  : %d\n", mod);
    printf("Minor    (Level 4)  : %d\n", minr);
    printf("================================\n");
}

/* ===================== MENU ===================== */
void printMenu(void) {
    printf("\n========================================\n");
    printf("      PATIENT MANAGEMENT SYSTEM\n");
    printf("========================================\n");
    printf("1.  Register New Patient\n");
    printf("2.  Display All Patients\n");
    printf("3.  Search Patient\n");
    printf("4.  Update Patient\n");
    printf("5.  Delete Patient\n");
    printf("6.  Display Waiting Patients (Normal Queue)\n");
    printf("7.  Display Emergency Queue\n");
    printf("8.  Treat Next Patient\n");
    printf("9.  Change Patient Status\n");
    printf("10. Discharge Patient\n");
    printf("11. Filter Patients\n");
    printf("12. Sort Patients\n");
    printf("13. View Patient Medical History\n");
    printf("14. Add Medical History Entry\n");
    printf("15. Display Statistics\n");
    printf("0.  Exit\n");
    printf("========================================\n");
    printf("Enter choice: ");
}

/* ===================== MAIN ===================== */
int main(void) {
    initQueue(&normalQ);
    initQueue(&priorityQ);

    printf("Starting Patient Management System...\n");
    if (!dbConnect()) {
        printf("Cannot continue without database. Exiting.\n");
        return 1;
    }

    loadPatientsFromDB();

    char buf[20];
    int choice;

    while (1) {
        printMenu();
        safeInput(buf, sizeof(buf));
        choice = atoi(buf);

        switch (choice) {
            case 1:  registerPatient(); break;
            case 2:  displayAllPatients(); break;
            case 3:  searchPatient(); break;
            case 4:  updatePatient(); break;
            case 5:  deletePatient(); break;
            case 6:  displayQueue(&normalQ, "NORMAL WAITING QUEUE (Level 3 & 4)"); break;
            case 7:  displayEmergencyQueue(); break;
            case 8:  treatNextPatient(); break;
            case 9:  changeStatus(); break;
            case 10: dischargePatient(); break;
            case 11: filterPatients(); break;
            case 12: sortPatients(); break;
            case 13: viewHistory(); break;
            case 14: addHistoryManual(); break;
            case 15: displayStatistics(); break;
            case 0:
                printf("Saving and exiting...\n");
                freeEntireList();
                freeQueue(&normalQ);
                freeQueue(&priorityQ);
                dbClose();
                printf("Goodbye.\n");
                return 0;
            default:
                printf("Invalid choice. Try again.\n");
        }
    }
    return 0;
}

