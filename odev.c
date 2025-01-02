#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <pthread.h>

#define INPUT_CHARACTER_LIMIT 30
#define OUTPUT_CHARACTER_LIMIT 200
#define PORT_NUMBER 60000
#define LEVENSHTEIN_LIST_LIMIT 5
#define DICTIONARY_FILE "basic_words_2000.txt"

pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char word[INPUT_CHARACTER_LIMIT];
    char result[INPUT_CHARACTER_LIMIT];
    char (*dictionary)[INPUT_CHARACTER_LIMIT];
    int word_count;
    int index;
    int client_socket;
} WordThreadData;

typedef struct {
    int client_socket;
    char dictionary[2500][INPUT_CHARACTER_LIMIT];
    int *word_count;
} ClientArgs;

typedef struct {
    char word[INPUT_CHARACTER_LIMIT];
    int distance;
} WordDistance;

// Calculate Levenshtein distance between two words
int levenshtein_distance(const char *word1, const char *word2) {
    int len1 = strlen(word1), len2 = strlen(word2);
    int matrix[len1 + 1][len2 + 1];

    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (word1[i - 1] == word2[j - 1]) ? 0 : 1;
            matrix[i][j] = fmin(fmin(
                matrix[i - 1][j] + 1,
                matrix[i][j - 1] + 1),
                matrix[i - 1][j - 1] + cost);
        }
    }
    return matrix[len1][len2];
}

// Convert a string to lowercase
void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

// Remove leading and trailing whitespaces
void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

// Load the dictionary from a file
void load_dictionary(char words[][INPUT_CHARACTER_LIMIT], int *word_count) {
    FILE *file = fopen(DICTIONARY_FILE, "r");
    if (!file) {
        perror("ERROR: Dictionary file not found!");
        exit(EXIT_FAILURE);
    }
    *word_count = 0;
    while (fscanf(file, "%s", words[*word_count]) != EOF) {
        to_lowercase(words[*word_count]);
        (*word_count)++;
    }
    fclose(file);
}

// Add a new word to the dictionary and sort alphabetically
void add_to_dictionary(const char *word, char dictionary[][INPUT_CHARACTER_LIMIT], int *word_count) {
    if (*word_count >= 2500) {
        fprintf(stderr, "WARNING: Dictionary size limit reached. Cannot add more words.\n");
        return;
    }

    // Add the word to the dictionary
    strcpy(dictionary[*word_count], word);
    (*word_count)++;

    // Sort the dictionary alphabetically
    for (int i = 0; i < *word_count - 1; i++) {
        for (int j = i + 1; j < *word_count; j++) {
            if (strcmp(dictionary[i], dictionary[j]) > 0) {
                char temp[INPUT_CHARACTER_LIMIT];
                strcpy(temp, dictionary[i]);
                strcpy(dictionary[i], dictionary[j]);
                strcpy(dictionary[j], temp);
            }
        }
    }

    // Write the sorted dictionary back to the file
    FILE *file = fopen(DICTIONARY_FILE, "w");
    if (!file) {
        perror("ERROR: Could not open dictionary file for writing!");
        return;
    }

    for (int i = 0; i < *word_count; i++) {
        fprintf(file, "%s\n", dictionary[i]);
    }
    fclose(file);
}

// Find the closest words based on Levenshtein distance
void find_closest_words(const char *input_word, char dictionary[][INPUT_CHARACTER_LIMIT], int word_count, WordDistance closest[]) {
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++) {
        closest[i].distance = INT_MAX;
        strcpy(closest[i].word, "");
    }

    for (int i = 0; i < word_count; i++) {
        int distance = 0;
        if (strcmp(input_word, dictionary[i]) == 0) {
            // If the word is in the dictionary, set distance to 0
            distance = 0;
        } else {
            // Calculate Levenshtein distance
            distance = levenshtein_distance(input_word, dictionary[i]);
        }

        for (int j = 0; j < LEVENSHTEIN_LIST_LIMIT; j++) {
            if (distance < closest[j].distance) {
                for (int k = LEVENSHTEIN_LIST_LIMIT - 1; k > j; k--) {
                    closest[k] = closest[k - 1];
                }
                strcpy(closest[j].word, dictionary[i]);
                closest[j].distance = distance;
                break;
            }
        }
    }
}


// Check if a word exists in the dictionary
int is_in_dictionary(const char *word, char dictionary[][INPUT_CHARACTER_LIMIT], int word_count) {
    for (int i = 0; i < word_count; i++) {
        if (strcmp(word, dictionary[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int contains_invalid_characters(const char *input) {
    for (int i = 0; input[i] != '\0'; i++) {
        if (!isalpha((unsigned char)input[i]) && !isspace((unsigned char)input[i])) {
            return 1; // Found invalid character
        }
    }
    return 0;  // All characters are valid
}
void *process_word(void *arg) {
    WordThreadData *data = (WordThreadData *)arg;
    WordDistance closest[LEVENSHTEIN_LIST_LIMIT];
    char output[OUTPUT_CHARACTER_LIMIT];

    pthread_mutex_lock(&client_mutex);

    to_lowercase(data->word);


    if (is_in_dictionary(data->word, data->dictionary, data->word_count)) {
        sprintf(output, "WORD '%s' is already in the dictionary. Distance: 0\n", data->word);
        send(data->client_socket, output, strlen(output), 0);


        find_closest_words(data->word, data->dictionary, data->word_count, closest);

        strcpy(output, "Closest suggestions:\n");
        for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++) {
            char suggestion[OUTPUT_CHARACTER_LIMIT];
            sprintf(suggestion, "%d. %s (Distance: %d)\n", i + 1, closest[i].word, closest[i].distance);
            strcat(output, suggestion);
        }
        send(data->client_socket, output, strlen(output), 0);


        strcpy(data->result, data->word);
    } else {
        sprintf(output, "WORD '%s' is not present in dictionary.\n", data->word);
        send(data->client_socket, output, strlen(output), 0);

        find_closest_words(data->word, data->dictionary, data->word_count, closest);

        strcpy(output, "Closest suggestions:\n");
        for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++) {
            char suggestion[OUTPUT_CHARACTER_LIMIT];
            sprintf(suggestion, "%d. %s (Distance: %d)\n", i + 1, closest[i].word, closest[i].distance);
            strcat(output, suggestion);
        }
        send(data->client_socket, output, strlen(output), 0);

        sprintf(output, "Do you want to add this word to dictionary? (y/N) or type the number of a suggestion: ");
        send(data->client_socket, output, strlen(output), 0);

        char response[10];
        recv(data->client_socket, response, sizeof(response), 0);
        response[strcspn(response, "\n")] = '\0';

        if (isdigit(response[0])) {
            int choice = atoi(response);
            if (choice > 0 && choice <= LEVENSHTEIN_LIST_LIMIT) {
                strcpy(data->result, closest[choice - 1].word);
            } else {
                strcpy(data->result, data->word);
            }
        } else if (tolower(response[0]) == 'y') {
            add_to_dictionary(data->word, data->dictionary, &data->word_count);
            strcpy(data->result, data->word);
        } else {
            strcpy(data->result, closest[0].word);
        }
    }

    pthread_mutex_unlock(&client_mutex);
    pthread_exit(NULL);
}


void handle_connection(int client_socket, char dictionary[][INPUT_CHARACTER_LIMIT], int *word_count) {
    char input[INPUT_CHARACTER_LIMIT], original_input[INPUT_CHARACTER_LIMIT];
    pthread_t thread_ids[INPUT_CHARACTER_LIMIT];
    WordThreadData thread_data[INPUT_CHARACTER_LIMIT];
    int thread_count = 0;

    send(client_socket, "Hello, this is Text Analysis Server!\nPlease enter your input string:\n", 70, 0);
    recv(client_socket, input, INPUT_CHARACTER_LIMIT, 0);

    trim_whitespace(input);


    if (strlen(input) > INPUT_CHARACTER_LIMIT - 1) {
        send(client_socket, "ERROR: Input exceeds the 30-character limit!\n", 46, 0);
        close(client_socket);
        return;
    }


    if (contains_invalid_characters(input)) {
        send(client_socket, "ERROR: Input contains invalid characters! Only alphabet and spaces are allowed.\n", 82, 0);
        close(client_socket);
        return;
    }
    to_lowercase(input);
    strcpy(original_input, input);

    char *words[INPUT_CHARACTER_LIMIT];
    int word_count_in_input = 0;

    char *token = strtok(input, " ");
    while (token != NULL) {
        if (strlen(token) > 0) {
            words[word_count_in_input++] = strdup(token);
        }
        token = strtok(NULL, " ");
    }

    for (int i = 0; i < word_count_in_input; i++) {
        thread_data[i].index = i; 
        thread_data[i].word_count = *word_count;
        strcpy(thread_data[i].word, words[i]);
        thread_data[i].dictionary = dictionary;
        thread_data[i].client_socket = client_socket;

        if (pthread_create(&thread_ids[thread_count], NULL, process_word, (void *)&thread_data[i]) != 0) {
            perror("ERROR: Could not create thread for word processing");
            continue;
        }
        thread_count++;
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    char corrected_sentence[INPUT_CHARACTER_LIMIT] = "";
    for (int i = 0; i < word_count_in_input; i++) {
        for (int j = 0; j < thread_count; j++) {
            if (thread_data[j].index == i) { 
                strcat(corrected_sentence, thread_data[j].result);
                strcat(corrected_sentence, " ");
                break;
            }
        }
    }

    if (strlen(corrected_sentence) > 0 && corrected_sentence[strlen(corrected_sentence) - 1] == ' ') {
        corrected_sentence[strlen(corrected_sentence) - 1] = '\0';
    }

    char output[OUTPUT_CHARACTER_LIMIT * 10];
    sprintf(output, "INPUT: %s\nOUTPUT: %s\n", original_input, corrected_sentence);
    send(client_socket, output, strlen(output), 0);

    send(client_socket, "Thank you for using Text Analysis Server! Good Bye!\n", 52, 0);
    close(client_socket);
}


// Handle client threads
void *client_handler(void *arg) {
    ClientArgs *args = (ClientArgs *)arg;
    handle_connection(args->client_socket, args->dictionary, args->word_count);
    free(arg);
    return NULL;
}
int main() {
    char dictionary[2500][INPUT_CHARACTER_LIMIT];
    int word_count;

    // Load dictionary from file
    load_dictionary(dictionary, &word_count);

    // Create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("ERROR: Could not create socket");
        return 1;
    }

    struct sockaddr_in server_address = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT_NUMBER),
        .sin_addr.s_addr = INADDR_ANY
    };

    // Bind socket to the address
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("ERROR: Bind failed");
        return 1;
    }

    // Start listening for connections
    listen(server_socket, 3);
    printf("Server is running on port %d...\n", PORT_NUMBER);

    while (1) {

        // Accept a new client connection
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("ERROR: Accept failed");
            return 1;
        }

        // Create a thread for the client
        pthread_t thread_id;
        ClientArgs *args = malloc(sizeof(ClientArgs));
        if (!args) {
            perror("ERROR: Memory allocation failed for client arguments");
            close(client_socket);
            continue;
        }

        args->client_socket = client_socket;
        memcpy(args->dictionary, dictionary, sizeof(dictionary));
        args->word_count = &word_count;

        if (pthread_create(&thread_id, NULL, client_handler, (void *)args) != 0) {
            perror("ERROR: Could not create thread");
            close(client_socket);
            free(args);
            continue;
        }

        pthread_detach(thread_id); 
    }

    close(server_socket);
    return 0;
}