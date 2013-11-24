#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <termios.h>
#include <unistd.h>

static void disable_echo() {
  struct termios t;

  tcgetattr(0, &t);

  t.c_lflag &= ~ECHO;

  tcsetattr(0, TCSANOW, &t);
}

static void enable_echo() {
  struct termios t;

  tcgetattr(0, &t);

  t.c_lflag |= ECHO;

  tcsetattr(0, TCSANOW, &t);
}

static void read_password(char password[static 256]) {
  disable_echo();

  password[255] = 0;

  if (!fgets(password, 255, stdin)) {
    enable_echo();

    printf("\n");

    if (errno == 0) errx(EXIT_FAILURE, "End of file while reading password");

    err(EXIT_FAILURE, "Error reading password: %s", strerror(errno));
  }

  password[strlen(password) - 1] = 0; /* Remove \n */

  enable_echo();

  printf("\n");
}

void gensalt(char *salt) {
  FILE *f;
  unsigned int i;

  if (0 == (f = fopen("/dev/urandom", "r")))
    err(EXIT_FAILURE, "Failed to open /dev/urandom for reading");

  strcpy(salt, "$6$");

  if (9 != fread(salt + 3, 1, 9, f)) errx(EXIT_FAILURE, "Short fread");

  for (i = 0; i < 9; ++i) {
    salt[i + 3] = (salt[i + 3] & 0x7f) | 0x40;

    if (!isalpha(salt[i + 3])) salt[i + 3] = 'a' + rand() % ('z' - 'a');
  }

  salt[12] = '$';
  salt[13] = 0;

  fclose(f);
}

int main(int argc, char **argv) {
  char password[256];
  char salt[64];

  printf("Password: ");
  fflush(stdout);
  read_password(password);
  gensalt(salt);

  printf("%s\n", crypt(password, salt));

  return EXIT_SUCCESS;
}
