// Program napisany na zaliczenie przedmiotu SO
// WI ZUT
// PH
#include <stdio.h> // fopen
#include <stdlib.h> // exit
#include <unistd.h> // getopt
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include "matchrx.h"

// #ifndef __GNUC__
// #   define  __attribute__(x)
// #endif


static const char *DEFAULT_LOG_FILE = "/tmp/lcrond.log";
static const char *DEFAULT_CONFIG_FILE = "/etc/lcron.conf";
static const char *OUTPUT_FILES = "/tmp";

static const int month_days[] = { 0,
	31, 28, 31,
	30, 31, 30,
	31, 31,	30,
	31, 30, 31
};


struct Job {
	char *M, *H, *d, *m, *w;
	char *command;
	struct Job * next;
	int run;
	char *name;
};

struct Job * head = NULL;
struct Job * tail = NULL;

// Zmienne globalne
char *program_name;
int verbose = 0;
int foreground = 0;
char *config_path;
FILE *log_file;


void signal_handler(int);
void log_message(const int, const char *, ...);
// void log_message(const char *, const char *format, ...)
// 	__attribute__ ((format(printf, 0, 1)));

void usage(void);
void parse_args(int, char *[]);
void parse_config(const char *);

int get_next_run(struct Job *);

int parse_field(char *, const int, int);
int get_next_chunk(char *, char *, int);

void add_job(struct Job);
int next_jobs(void);
void run_jobs(void);
void run_job(struct Job *);


// Przechodzi w tryb demona i uruchamia zadania
int main(int argc, char *argv[])
{
	program_name = argv[0];

	pid_t child, session;

	if (!foreground) {
		child = fork();

		if (child < 0) {
			fprintf(stderr, "%s: cannot fork", program_name);
			exit(EXIT_FAILURE);
		}

		if (child > 0)
			exit(EXIT_SUCCESS);
	}

	// Pełna kontrola nad plikami stworzonymi przez demona
	umask(0);

	// Nowa sesja dla dziecka
	if (!foreground) {
		session = setsid();
		if (session < 0) {
			fprintf(stderr, "%s: cannot set session\n", program_name);
			exit(EXIT_FAILURE);
		}
	}

	// Zmiana aktualnie używanego katalogu
	if (chdir("/") < 0) {
		fprintf(stderr, "%s: cannot change working directory\n", program_name);
		exit(EXIT_FAILURE);
	}

	// Przechwyć sygnały
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Instrukcje inicjaliujące */

	parse_args(argc, argv);

	log_message(0, "* Staring lcrond...");
	log_message(1, "Startup info: verbosity: %i, config file path: %s", verbose, config_path);

	// Czytamy plik konfiguracyjny i ustawiamy zadania
	parse_config(config_path);

	// Zamykanie niepotrzebnych strumieni
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	if (foreground == 0)
		close(STDERR_FILENO);

	int await = -1;
	// Zapętlamy się
	while (1) {
		await = next_jobs();
		if (await < 0) {
			log_message(0, "No proper jobs");
			exit(EXIT_FAILURE);
		}
		log_message(1, "Waiting %i seconds", await);
		sleep(await);
		run_jobs();
	}
}

// Zaloguj wiadomość wraz z aktualnym czasem
// http://www.codemaestro.com/reviews/18
void log_message(const int level, const char *message/*, const char *format*/, ...)
{
	if (level > verbose)
		return;
	// Otworzenie pliku do logów
	log_file = fopen(DEFAULT_LOG_FILE, "a");
	if (!log_file) {
		// fprintf(stderr, "%s: cannot create log file\n", program_name);
		perror("Cannot create log");
		exit(EXIT_FAILURE);
	}

	char timestamp[200];
	time_t now;
	struct tm *tmp;

	now = time(NULL);
	if (now < 0) {
		fprintf(log_file, "Cannot get current time\n");
		exit(EXIT_FAILURE);
	}

	tmp = localtime(&now);
	if (tmp == NULL ) {
		fprintf(log_file, "Cannot convert current time\n");
		exit(EXIT_FAILURE);
	}

	if (strftime(timestamp, sizeof(timestamp), "%d %b %H:%M:%S", tmp) == 0)
		sprintf(timestamp, "timestamp error");

	// Pozwól na przekazywanie argumentów jak do efprintefa.
	fprintf(log_file, "[%s] ", timestamp);
	va_list formats;
	va_start(formats, message);
	vfprintf(log_file, message, formats);
	va_end(formats);
	fprintf(log_file, "\n");

	fclose(log_file);
}

// Przechwytuje sygnały
void signal_handler(int signal)
{
	// switch(signal) {
	// case SIGHUP:
	// case SIGTERM:
	// 	break;
	// }
	log_message(0, "* Got signal %i, exitting", signal);
	exit(EXIT_SUCCESS);
}

// Wyświetla wszystkie opcje, które akcepuje program
void usage(void)
{
	fprintf(stderr, "Usage: %s [-f] [-v] [-c path]\n", program_name);
	fprintf(stderr, " -f foreground\n");
	fprintf(stderr, " -v verbose output (more 'v', more verbose)\n");
	fprintf(stderr, " -c path to configuration file\n");
	exit(EXIT_FAILURE);
}

// Przetwarza argumenty przekazane do programu
void parse_args(int argc, char *argv[])
{
	int option;
	while ((option = getopt(argc, argv, "c:fvb")) != -1) {
		switch (option) {
		case 'c':
			config_path = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'b':
			foreground = 0;
			break;
		default:
			usage();
		}
	}

	if (!config_path)
		config_path = (char *)DEFAULT_CONFIG_FILE;
}


// Przepisuje rekordy do listy zadań
void parse_config(const char *path)
{
	log_message(1, "Reading config file %s", path);
	FILE *config_file;
	config_file = fopen(path, "r");
	if (!config_file) {
		log_message(0, "Cannot read config file");
		perror("config file");
		exit(EXIT_FAILURE);
	}

	int await;
	int jobs = 0;
	struct Job job;

	char line[1000];
	char data[1000];

	while(fgets(line, 1000, config_file)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		// Bo zmienna zostaje podmieniona przez match_rx
		strcpy(data, line);
		// job = malloc(sizeof(struct Job));
		if (match_rx(
			"^[ \t]*([0-9,-\\*]+)[ \t]+([0-9,-\\*]+)[ \t]+" // minuty, godziny
			"([0-9,-\\*]+)[ \t]+([0-9,-\\*]+)[ \t]+([0-9,-\\*]+)[ \t]+"
			//"[a-zA-Z0-9]+[ \t]+" // Użytkownik
			"(.+)$", // polecenie
			data, 6, &job.M, &job.H, &job.d, &job.m, &job.w, &job.command))
		{
			await = get_next_run(&job);
			if (await < 0) {
				log_message(0, "Wrong record: %s", line);
				continue;
			}

			log_message(1, "Job test: wait %i seconds for %s", await, line);
			// Nadaj nawę zadaniu
			char name[] = "noname";
			sprintf(name, "job%i", jobs);
			job.name = malloc(sizeof(name));
			strcpy(job.name, name);
			add_job(job);
			jobs++;
		}
	}

	fclose(config_file);
	log_message(1, "File parsed");
	log_message(0, "Got %i jobs", jobs);
}

// Zwraca ilość sekund do kolejnego uruchomienia lub -1 przy błędzie
int get_next_run(struct Job *job)
{
	time_t now;
	struct tm *tmp;

	now = time(NULL);
	if (now < 0) {
		log_message(0, "Cannot get current time");
		exit(EXIT_FAILURE);
	}
	tmp = localtime(&now);
	if (tmp == NULL ) {
		log_message(0, "Cannot convert current time");
		exit(EXIT_FAILURE);
	}

	// Czy to zawsze będzie dobrze?
	int days_in_month = month_days[tmp->tm_mday];
	if (tmp->tm_mday == 2) {
		if((tmp->tm_year % 4 == 0 && tmp->tm_year % 100 != 0) || tmp->tm_year % 400 == 0)
			days_in_month + 1;
	}

	int seconds = 60;

	int minutes = parse_field(job->M, 60, tmp->tm_min);
	if (minutes < 0)
		return -1;
	seconds += minutes * 60;
	int hours = parse_field(job->H, 24, tmp->tm_hour);
	if (hours < 0)
		return -1;
	seconds += hours * 3600;
	int days = parse_field(job->d, days_in_month, tmp->tm_mday);
	if (days < 0)
		return -1;
	seconds += days * 86400;
	int months = parse_field(job->m, 12, tmp->tm_mon);
	if (months < 0)
		return -1;
	seconds += months * 86400 * days_in_month;
	int week_days = parse_field(job->w, 7, tmp->tm_wday);
	if (week_days < 0)
		return -1;
	seconds += week_days * 604800;

	return seconds;
}

// Zwracana wartość to liczba sekund do current_value lub
// -1 zła liczba
// -2 zły zakres
int parse_field(char *expression, int max_value, int now)
{
	log_message(2, "Parsing field: '%s'", expression);
	char chunk[100];
	char *more;
	char *value_s, *mul_s, *from_s, *to_s;
	int value, mul, from, to;
	int field_number = 0;
	int new_stuff_size;
	int i;
	int await, temp;
	await = max_value;
	// Dzieli pola oddzielone przecinkami na fragmenty a następnie je przetwarza
	// Przetworzone dane dodaje do listy danego pola
	while (1) {
		if (get_next_chunk(expression, chunk, field_number++) == 0) {
			log_message(2, "End of chunks");
			break;
		}
		log_message(2, "Parsing chunk: '%s'", chunk);
		// Podział (z zakresem) '(*|liczba-liczba)/liczba'
		if (match_rx("(\\*|[0-9]+-[0-9]+)/([0-9]+)", chunk, 2, &more, &mul_s)) {
			if (!mul_s)
				return -1;
			mul = atoi(mul_s);
			if (match_rx("([0-9]+)-([0-9]+)", more, 2, &from_s, &to_s)) {
				if (!from_s || !to_s)
					return -1;
				from = atoi(from_s);
				to = atoi(to_s);
			} else {
				from = 0;
				to = max_value;
			}
			log_message(2, "range with dzielenie: %i - %i / %i", from, to, mul);
			if (from > max_value || to > max_value || to == 0 || from >= to)
				return -2;
			// Trafiamy w liczbe
			if (from <= now && now < to && ((now - from) % mul) == 0)
				return 0;
			temp = max_value;
			for (value = from; value <= to; value += mul) {
				if (value - now > 0 && value - now < temp)
					temp = value - now;
			}
			if (temp == 0)
				return 1;
			else
				if (await > temp)
					await = temp;
		// Zakres 'liczba-liczba'
		} else if (match_rx("([0-9]+)-([0-9]+)", chunk, 2, &from_s, &to_s)) {
			if (!from_s || !to_s)
				return -1;
			from = atoi(from_s);
			to = atoi(to_s);
			log_message(2, "range: %i - %i", from, to);
			if (from > to || to == 0)
				return -2;
			if (from <= now && now < to)
				return 0;
			else {
				temp = (max_value + from - now) % max_value;
				if (await > temp)
					await = temp;
			}
		// Pojedyncze pole liczbowe
		} else if (match_rx("([0-9]+)", chunk, 1, &value_s)) {
			if(!value_s)
				return -1;
			value = atoi(value_s);
			log_message(2, "value: %i", value);
			if (value > max_value)
				return -2;
			// Ile jednostek do kolejnej takiej wartości
			temp = (max_value + value - now) % max_value;
			if (await > temp)
				await = temp;
		// Gwiazdka
		} else if (match_rx("*", chunk, 1)) {
			log_message(2, "asterisk: all values");
			return 0;
		} else {
			log_message(2, "Can't parse chunk");
			return -1;
		}
	}
	return await;
}

// Szuka kolejnego ciągu (field_number) pomiędzy przecinkami i zwraca je do output_str
// Zwraca iloćś skopiowanych znaków
int get_next_chunk(char *string, char *output_str, int field_number)
{
	int field_current = 0;
	char *start = string;
	// char *output = output_str;
	int i = 0;
	while(*start) {
		if( field_current > field_number) {
			output_str[i] = 0;
			break;
		}
		if (*start == ',') {
			field_current++;
			*start++;
		}
		if (field_current == field_number) {
			output_str[i++] = *start++;
		}
		if (field_current < field_number) {
			*start++;
			continue;
		}
	}
	return i;
}


// Dodaje zadanie do listy
void add_job(struct Job temp)
{
	struct Job *job = malloc(sizeof(struct Job));

	job->M = malloc(sizeof(temp.M));
	job->H = malloc(sizeof(temp.H));
	job->d = malloc(sizeof(temp.d));
	job->m = malloc(sizeof(temp.m));
	job->w = malloc(sizeof(temp.w));
	job->command = malloc(sizeof(temp.command));
	job->name = malloc(sizeof(temp.name));
	job->run = 0;

	strcpy(job->M, temp.M);
	strcpy(job->H, temp.H);
	strcpy(job->d, temp.d);
	strcpy(job->m, temp.m);
	strcpy(job->w, temp.w);
	strcpy(job->command, temp.command);
	strcpy(job->name, temp.name);

	job->next = NULL;

	if (tail != NULL) {
		// Ustawienie wskaźnika poprzedniej pracy na nową
		tail->next = job;
		// i ogona na właśnie dodaną
		tail = job;
	}

	if (head == NULL && tail == NULL) {
		head = job;
		tail = job;
	}

	log_message(2, "Job added to list: %s", job->command);
}


// Zwraca ilość sekund do uruchomienia najbliższych prac lub -1 przy błędzie
int next_jobs(void)
{
	int job_await;
	int await = -1;

	// Pierw szuka najkrótszego czasu pracy
	struct Job *current = head;
	while (current) {
		job_await = get_next_run(current);
		if (job_await < await || await == -1)
			await = job_await;
		log_message(2, "next_jobs: now await: %i", await);
		current = current->next;
	}

	if (await == -1)
		return -1;

	// Potem ustawia ją do uruchomienia
	current = head;
	while (current) {
		job_await = get_next_run(current);
		if (job_await == await)
			current->run = 1;
		current = current->next;
	}

	log_message(2, "Shortest time: %i", await);
	return await;
}

// Uruchamia zadania z ustawiąną flagą run
void run_jobs(void)
{
	struct Job *current = head;
	while (current) {
		log_message(2, "Job run flag: %i", current->run);
		if (current->run == 1)
			run_job(current);
		current->run = 0;
		current = current->next;
	}
}

void run_job(struct Job *job)
{
	log_message(1, "Running job: %s", job->command);
	pid_t run_job_child;

	run_job_child = fork();

	if (run_job_child < 0) {
		log_message(0, "Cannot put job to run: %s", job->command);
		return;
	}

	if (run_job_child == 0) {
		pid_t child;
		child = fork();

		char out_file[100];
		char err_file[100];
		sprintf(out_file, "%s/%s.stdout", OUTPUT_FILES, job->name);
		sprintf(err_file, "%s/%s.stderr", OUTPUT_FILES, job->name);

		int child_stdout = open(out_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
		int child_stderr = open(err_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

		dup2(child_stdout, 1);
		dup2(child_stderr, 2);

		if (child < 0) {
			log_message(0, "Cannot run job: %s", job->command);
			exit(EXIT_FAILURE);
		}
		if (child == 0) {
			execl("/bin/sh", "sh", "-c", job->command, 0);
			exit(EXIT_FAILURE);
		}
		if (child > 0) {
			int exit_status;
			if (waitpid(child, &exit_status, 0) < 0)
				log_message(0, "Child wait error");
			else
				log_message(0, "%s done, exit status: %i", job->name, exit_status);
		}

		close(child_stdout);
		close(child_stderr);

		log_message(1, "Job output is in files: %s, %s", out_file, err_file);
		exit(EXIT_SUCCESS);
	}

	if (run_job_child > 0) {
		// wait(run_job_child);
		log_message(1, "Job putted to run: %s", job->command);
	}
}
