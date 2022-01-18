#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define ftp "ftp://"
#define FTP_PORT 21
#define BufMaxSize 1024
#define defaultUser "anonymous"
#define defaultPass ""

// USED ONLY FOR THE PROGRESS BAR
#define fiftySpaces "                                                  "
#define fiftyEquals "=================================================="

typedef struct download
{
    char *user;
    char *pass;
    char *host;
    char *filepath;
    char *file;
} Download;

void progress_bar(size_t items_processed, size_t total_items, int progress_bar_size);
int get_download_details(const char *url, Download *download);
void free_download(Download *download);
/*
 * @brief parses input based on rfc959, stores message in buf
 * @param sockfd Socket File Descriptor
 * @param buf buffer used to receive server input (Should be empty)
 * @param len buffer length
 * @return on success returns code
 */
int getmessage(FILE *fp, char *buf, size_t len);

int main(int argc, char const *argv[])
{

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Download download;
    // Getting download details before connecting to server
    if (get_download_details(argv[1], &download) < 0)
    {
        perror("get_download_details");
        exit(EXIT_FAILURE);
    }

    printf("User:%s\n", download.user);
    printf("Host:%s\n", download.host);
    printf("Path:%s\n", download.filepath);

    // Getting host ip address
    struct hostent *h;
    if ((h = gethostbyname(download.host)) == NULL)
    {
        herror("gethostbyname()");
        return -1;
    }
    char *host_ipv4 = inet_ntoa(*((struct in_addr *)h->h_addr));
    printf("Host name: %s\n", h->h_name);
    printf("IP Address: %s\n", host_ipv4);

    struct sockaddr_in server_addr;
    /*server address handling*/
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(host_ipv4); /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(FTP_PORT);             /*server TCP port must be network byte ordered */

    int sockfd;
    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }
    /*connect to the server*/
    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect()");
        exit(EXIT_FAILURE);
    }

    FILE *fp = fdopen(sockfd, "r");
    char buf[BufMaxSize];

    bzero(buf, BufMaxSize);
    int code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 220)
    {
        fprintf(stderr, "Server is not ready: %s", buf);
        return code;
    }

    // Writing username
    bzero(buf, BufMaxSize);
    strcat(buf, "USER ");
    strcat(buf, download.user);
    strcat(buf, "\n");
    if (write(sockfd, buf, strlen(buf)) < 0)
    {
        perror("write()");
        exit(EXIT_FAILURE);
    }

    // Reading reply
    bzero(buf, BufMaxSize);
    code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 331)
    {
        fprintf(stderr, "Invalid Username: %s", buf);
        return code;
    }

    // Writing password
    bzero(buf, BufMaxSize);
    strcat(buf, "PASS ");
    strcat(buf, download.pass);
    strcat(buf, "\n");
    if (write(sockfd, buf, strlen(buf)) < 0)
    {
        perror("write()");
        exit(EXIT_FAILURE);
    }

    // Reading reply
    bzero(buf, BufMaxSize);
    code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 230)
    {
        fprintf(stderr, "Unable to Login: %s", buf);
        return code;
    }

    // Entering Passive Mode
    if (write(sockfd, "PASV\n", strlen("PASV\n")) < 0)
    {
        perror("write()");
        exit(EXIT_FAILURE);
    }

    // Reading reply
    bzero(buf, BufMaxSize);
    code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 227)
    {
        fprintf(stderr, "Could not get passive mode: %s", buf);
        return code;
    }

    // Getting ip address and port
    char dataTransferIP[BufMaxSize];
    uint16_t dataPort;

    // Getting ip
    char *buf_ptr = strchr(buf, '(') + 1;
    int j = 0; // Used for string manipulation
    for (int i = 0; i < 4; i++)
    {
        while (*buf_ptr != ',')
        {
            dataTransferIP[j] = *buf_ptr;
            buf_ptr++;
            j++;
        }
        buf_ptr++;
        dataTransferIP[j] = '.';
        j++;
    }
    j--;
    dataTransferIP[j] = '\0';
    printf("Data Transfer IP: %s\n", dataTransferIP);

    // Getting port
    j = 0;
    char dataPort_str[BufMaxSize];
    while (*buf_ptr != ',')
    {
        dataPort_str[j] = *buf_ptr;
        buf_ptr++;
        j++;
    }
    buf_ptr++;
    dataPort_str[j] = '\0';
    dataPort = atoi(dataPort_str) * 256;
    j = 0;
    while (*buf_ptr != ')')
    {
        dataPort_str[j] = *buf_ptr;
        buf_ptr++;
        j++;
    }
    dataPort_str[j] = '\0';
    dataPort += atoi(dataPort_str);
    printf("Data Transfer Port: %d\n", dataPort);

    // Creating socket to connect to <Passive IP>:<Passive Port>
    struct sockaddr_in passive_addr;
    /*server address handling*/
    bzero((char *)&passive_addr, sizeof(passive_addr));
    passive_addr.sin_family = AF_INET;
    passive_addr.sin_addr.s_addr = inet_addr(dataTransferIP); /*32 bit Internet address network byte ordered*/
    passive_addr.sin_port = htons(dataPort);                  /*server TCP port must be network byte ordered */

    int dataSockFD;
    /*open a TCP socket*/
    if ((dataSockFD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }
    /*connect to the server (no confirmation message is sent)*/
    if (connect(dataSockFD,
                (struct sockaddr *)&passive_addr,
                sizeof(passive_addr)) < 0)
    {
        perror("connect()");
        exit(EXIT_FAILURE);
    }

    // Writing retrieve file command
    bzero(buf, BufMaxSize);
    strcat(buf, "RETR ");
    strcat(buf, download.filepath);
    strcat(buf, "\n");
    if (write(sockfd, buf, strlen(buf)) < 0)
    {
        perror("write()");
        exit(EXIT_FAILURE);
    }

    // Reading reply
    bzero(buf, BufMaxSize);
    code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 150)
    {
        fprintf(stderr, "Unable to data connection: %s", buf);
        return code;
    }

    size_t file_size = 0;
    // Getting file size
    char *occurrence = strrchr(buf, '(');
    if (occurrence != NULL)
    {
        file_size = atoi(occurrence + 1);
        printf("Downloading %s (%ld bytes)\n", download.file, file_size);
    }

    // Reading file contents
    FILE *file_to_transfer, *dataFP;
    dataFP = fdopen(dataSockFD, "rb");
    file_to_transfer = fopen(download.file, "wb");
    size_t total_bytes_read = 0;
    while (!feof(dataFP))
    {
        bzero(buf, BufMaxSize);
        int bytes_read = fread(buf, 1, BufMaxSize, dataFP);
        total_bytes_read += bytes_read;
        if (bytes_read < 0)
        {
            perror("fread()");
            return bytes_read;
        }
        fwrite(buf, 1, bytes_read, file_to_transfer);
        if (file_size != 0)
            progress_bar(total_bytes_read, file_size, 50);
    }
    if (file_size != 0)
        printf("\n");
    fclose(file_to_transfer);

    // Reading transfer complete confirmation
    bzero(buf, BufMaxSize);
    code = getmessage(fp, buf, BufMaxSize);
    if (code < 0)
    {
        perror("getmessage()");
        return code;
    }
    else if (code != 226)
    {
        fprintf(stderr, "Unable to data connection: %s", buf);
        return code;
    }

    free_download(&download);
    if (close(sockfd) < 0)
    {
        perror("close()");
        exit(EXIT_FAILURE);
    }
    if (close(dataSockFD) < 0)
    {
        perror("close()");
        exit(EXIT_FAILURE);
    }

    if (file_size != 0 && file_size != total_bytes_read)
    {
        perror("Expected file and received file dont match");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int get_download_details(const char *url, Download *download)
{
    char buf[BufMaxSize];

    strncpy(buf, url, BufMaxSize);
    buf[BufMaxSize] = '\0';

    if (strncmp(buf, ftp, strlen(ftp)) != 0)
    {
        fprintf(stderr, "Invalid Protocol\n");
        exit(EXIT_FAILURE);
    }
    char *curpos = buf + strlen(ftp);

    // Getting Username and Password
    char *occurrence;
    if ((occurrence = strchr(curpos, '@')) != NULL)
    {
        char *separation = strchr(curpos, ':');
        if (separation == NULL)
        {
            perror("strchr() 3");
            return -1;
        }
        download->user = strndup(curpos, separation - curpos);
        curpos = separation + 1;
        download->pass = strndup(curpos, occurrence - curpos);
        curpos = occurrence + 1;
    }
    else
    {
        download->user = strdup(defaultUser);
        download->pass = strdup(defaultPass);
    }

    // Getting host
    occurrence = strchr(curpos, '/');
    if (occurrence == NULL)
    {
        perror("strchr() 1");
        return -1;
    }
    download->host = strndup(curpos, occurrence - curpos);

    // Getting filepath
    curpos = occurrence + 1;
    download->filepath = strdup(curpos);
    // Getting path

    occurrence = strrchr(download->filepath, '/');
    if (occurrence == NULL)
    {
        printf(" %s\n", download->filepath);
        char* file = malloc(strlen(download->filepath) + 1);
        strcpy(file, download->filepath);
        download->file = file;
    }
    download->file = strdup(occurrence + 1);
    return 0;
}

void free_download(Download *download)
{
    free(download->user);
    free(download->pass);
    free(download->host);
    free(download->filepath);
    free(download->file);
}

int getmessage(FILE *fp, char *buf, size_t len)
{
    char *cur_pos = fgets(buf, BufMaxSize, fp);
    if (cur_pos == NULL)
        return -1;
    char code_str[4];
    // Reading code of message (first 3 chars)
    strncpy(code_str, buf, 3);

    code_str[3] = '\0';

    while (cur_pos[3] == '-')
    {
        cur_pos += strlen(cur_pos);
        cur_pos = fgets(cur_pos, BufMaxSize - (cur_pos - buf), fp);
        if (cur_pos == NULL)
            return -1;
    }
    return atoi(code_str);
}

void progress_bar(size_t items_processed, size_t total_items, int progress_bar_size)
{
    int x = progress_bar_size * items_processed / total_items;
    int percentage = 100 * items_processed / total_items;
    printf("\r[%.*s%.*s]%d%%", x, fiftyEquals, (progress_bar_size - x), fiftySpaces, percentage);
    fflush(stdout);
}