#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "target.h"
#include "client.h"
#include "range.h"
#include "util.h"
#include "xlink.h"
#include "machine.h"

#define COMMAND_NONE       0x00
#define COMMAND_LOAD       0x01
#define COMMAND_SAVE       0x02
#define COMMAND_POKE       0x03
#define COMMAND_PEEK       0x04
#define COMMAND_JUMP       0x05
#define COMMAND_RUN        0x06
#define COMMAND_RESET      0x07
#define COMMAND_HELP       0x08
#define COMMAND_READY      0x0e
#define COMMAND_PING       0x0f
#define COMMAND_BOOTLOADER 0x10
#define COMMAND_BENCHMARK  0x11
#define COMMAND_IDENTIFY   0x12
#define COMMAND_SERVER     0x13
#define COMMAND_RELOCATE   0x14
#define COMMAND_KERNAL     0x15
#define COMMAND_FILL       0x16

#define MODE_EXEC 0x00
#define MODE_HELP 0x01

int mode  = MODE_EXEC;

static struct option options[] = {
  {"help",    no_argument,       0, 'h'},
  {"verbose", no_argument,       0, 'v'},
  {"quiet",   no_argument,       0, 'q'},
  {"device",  required_argument, 0, 'd'},
  {"machine", required_argument, 0, 'M'},
  {"memory",  required_argument, 0, 'm'},
  {"bank",    required_argument, 0, 'b'},
  {"address", required_argument, 0, 'a'},
  {"skip",    required_argument, 0, 's'},
  {"force",   required_argument, 0, 'f'},
  {0, 0, 0, 0}
};

//------------------------------------------------------------------------------

char str2id(const char* arg) {
  if (strcmp(arg, "load"      ) == 0) return COMMAND_LOAD;
  if (strcmp(arg, "save"      ) == 0) return COMMAND_SAVE;
  if (strcmp(arg, "poke"      ) == 0) return COMMAND_POKE;
  if (strcmp(arg, "peek"      ) == 0) return COMMAND_PEEK;
  if (strcmp(arg, "jump"      ) == 0) return COMMAND_JUMP;
  if (strcmp(arg, "run"       ) == 0) return COMMAND_RUN;  
  if (strcmp(arg, "reset"     ) == 0) return COMMAND_RESET;  
  if (strcmp(arg, "help"      ) == 0) return COMMAND_HELP;  
  if (strcmp(arg, "ready"     ) == 0) return COMMAND_READY;  
  if (strcmp(arg, "ping"      ) == 0) return COMMAND_PING;  
  if (strcmp(arg, "bootloader") == 0) return COMMAND_BOOTLOADER;  
  if (strcmp(arg, "benchmark" ) == 0) return COMMAND_BENCHMARK;  
  if (strcmp(arg, "identify"  ) == 0) return COMMAND_IDENTIFY;
  if (strcmp(arg, "server"    ) == 0) return COMMAND_SERVER;
  if (strcmp(arg, "relocate"  ) == 0) return COMMAND_RELOCATE;
  if (strcmp(arg, "kernal"    ) == 0) return COMMAND_KERNAL;      
  if (strcmp(arg, "fill"      ) == 0) return COMMAND_FILL;      

  return COMMAND_NONE;
}

//------------------------------------------------------------------------------

char* id2str(const char id) {
  if (id == COMMAND_NONE)       return (char*) "main";
  if (id == COMMAND_LOAD)       return (char*) "load";
  if (id == COMMAND_SAVE)       return (char*) "save";
  if (id == COMMAND_POKE)       return (char*) "poke";
  if (id == COMMAND_PEEK)       return (char*) "peek";
  if (id == COMMAND_JUMP)       return (char*) "jump";
  if (id == COMMAND_RUN)        return (char*) "run";
  if (id == COMMAND_RESET)      return (char*) "reset";
  if (id == COMMAND_HELP)       return (char*) "help";
  if (id == COMMAND_READY)      return (char*) "ready";  
  if (id == COMMAND_PING)       return (char*) "ping";  
  if (id == COMMAND_BOOTLOADER) return (char*) "bootloader";  
  if (id == COMMAND_BENCHMARK)  return (char*) "benchmark";  
  if (id == COMMAND_IDENTIFY)   return (char*) "identify";
  if (id == COMMAND_SERVER)     return (char*) "server";
  if (id == COMMAND_RELOCATE)   return (char*) "relocate";
  if (id == COMMAND_KERNAL)     return (char*) "kernal";      
  if (id == COMMAND_FILL)       return (char*) "fill";      
  return (char*) "unknown";
}

//------------------------------------------------------------------------------

int isCommand(const char *str) {
  return str2id(str) > COMMAND_NONE;
}

//------------------------------------------------------------------------------

int isOption(const char *str) {
  return str[0] == '-';
}

//------------------------------------------------------------------------------

int isOptarg(const char* option, const char* argument) {

  if (!isOption(option)) {
      return false;
  }

  for(int i=0; options[i].name != 0; i++) {
    
    if (!options[i].has_arg) {
      continue;
    }
      
    if(strlen(option) == 2) {
      if(option[1] == options[i].val) {
	return true;
      }
    }
    
    if(strlen(option) > 2) {
      if (option[2] == options[i].val) {
	return true;
      }
    }
  } 
  
  return false;
}

//------------------------------------------------------------------------------

int valid(int address) {
  return address >= 0x0000 && address <= 0x10000; 
}

//------------------------------------------------------------------------------

void screenOn(void) {
  xlink_poke(machine->memory, machine->bank, 0xd011, 0x1b);
}

//------------------------------------------------------------------------------

void screenOff(void) {
  xlink_poke(machine->memory, machine->bank, 0xd011, 0x0b);
}

//------------------------------------------------------------------------------

Commands* commands_new(int argc, char **argv) {

  Commands* commands = (Commands*) calloc(1, sizeof(Commands));
  commands->count = 0;
  commands->items = (Command**) calloc(1, sizeof(Command*));

  while(argc > 0) {
    commands_add(commands, command_new(&argc, &argv));
  }  

  return commands;
}

//------------------------------------------------------------------------------

Command* commands_add(Commands* self, Command* command) {
  self->items = (Command**) realloc(self->items, (self->count+1) * sizeof(Command*));
  self->items[self->count] = command;
  self->count++;
  return command;
}

//------------------------------------------------------------------------------

bool commands_each(Commands* self, bool (*func) (Command* command)) {
  bool result = true;

  for(int i=0; i<self->count; i++) {
    if(!(result = func(self->items[i]))) {
      break;
    }
  }
  return result;
}

//------------------------------------------------------------------------------

bool commands_execute(Commands* self) {
  return commands_each(self, &command_execute);
}

//------------------------------------------------------------------------------

void commands_print(Commands* self) {
  commands_each(self, &command_print);
}

//------------------------------------------------------------------------------

void commands_free(Commands* self) {

  for(int i=0; i<self->count; i++) {
    command_free(self->items[i]);
  }  
  free(self->items);
  free(self);
}

//------------------------------------------------------------------------------

Command* command_new(int *argc, char ***argv) {

  Command* command = (Command*) calloc(1, sizeof(Command));

  command->id        = COMMAND_NONE;
  command->name      = NULL;
  command->memory    = 0xff;
  command->bank      = 0xff;
  command->start     = -1;
  command->end       = -1;
  command->skip      = -1;
  command->force     = false;
  command->argc      = 0;
  command->argv      = (char**) calloc(1, sizeof(char*));
  
  command_append_argument(command, (char*)"getopt");
  command_consume_arguments(command, argc, argv);

  return command;
}

void command_free(Command* self) {

  free(self->name);

  self->argc += self->offset;
  self->argv -= self->offset;

  for(int i=0; i<self->argc; i++) {
    free(self->argv[i]);
  }
  free(self->argv);
  free(self);
}

//------------------------------------------------------------------------------
int command_arity(Command* self) {

  if (self->id == COMMAND_NONE)       return -1;
  if (self->id == COMMAND_LOAD)       return 1;
  if (self->id == COMMAND_SAVE)       return 1;
  if (self->id == COMMAND_POKE)       return 1;
  if (self->id == COMMAND_PEEK)       return 1;
  if (self->id == COMMAND_JUMP)       return 1;
  if (self->id == COMMAND_RUN)        return 1;
  if (self->id == COMMAND_RESET)      return 0;
  if (self->id == COMMAND_HELP)       return 1;
  if (self->id == COMMAND_READY)      return 0;
  if (self->id == COMMAND_PING)       return 0;
  if (self->id == COMMAND_BOOTLOADER) return 0;
  if (self->id == COMMAND_BENCHMARK)  return 0;
  if (self->id == COMMAND_IDENTIFY)   return 0;
  if (self->id == COMMAND_SERVER)     return 1;
  if (self->id == COMMAND_RELOCATE)   return 1;
  if (self->id == COMMAND_KERNAL)     return 2;    
  if (self->id == COMMAND_FILL)       return 2;    
  return 0;

}
//------------------------------------------------------------------------------
void command_consume_arguments(Command *self, int *argc, char ***argv) {

#define hasNext (*argc) > 0
#define next (*argc)--, (*argv)++, isFirst=false
#define current (*argv[0])
#define hasPrevious !isFirst
#define previous (*(*(argv)-1))

  bool isFirst = true;  
  size_t len = strlen(current);  

  self->name = (char *) calloc(len+1, sizeof(char));
  strncpy(self->name, current, len);

  self->id = str2id(self->name);

  if(isCommand(self->name)) {
    next;
  }

  int arity = command_arity(self);
  int consumed = 0;

  for(;hasNext;next) {

    if(isCommand(current) && !isOptarg(previous, current)) {
      break;
    }
    
    if (consumed == arity && arity > 0) {
      if (hasPrevious && !isOptarg(previous, current)) {
        break;
      }
      else if (!isOption(current)) {
        break;
      }
    }

    command_append_argument(self, current);

    if (consumed < arity) {
      if (hasPrevious && isOptarg(previous, current)) {
        continue;
      }
      else if (isOption(current)) {
        continue;
      }
      consumed+=1;      
    }    
  }
}

//------------------------------------------------------------------------------

void command_append_argument(Command* self, char* arg) {
  self->argv = (char**) realloc(self->argv, (self->argc+1) * sizeof(char*));
  size_t len = strlen(arg);
  self->argv[self->argc] = (char*) calloc(len+1, sizeof(char));
  strncpy(self->argv[self->argc], arg, len+1);
  self->argc++;
}

//------------------------------------------------------------------------------

bool command_parse_options(Command *self) {
  
  int option, index;
  char *end;
  
  optind = 0;
  
  while(1) {

    option = getopt_long(self->argc, self->argv, "hvqfd:M:m:b:a:s:", options, &index);
    
    if(option == -1)
      break;

    switch(option) {

    case 'h':
      usage();
      break;

    case 'q':
      logger->set("NONE");
      break;

    case 'v':
      logger->set("ALL");
      break;

    case 'd':
      if (!xlink_set_device(optarg)) {
        return false; 
      }
      break;

    case 'M':
      if(strncasecmp(optarg, "c64", 3) == 0) {
	machine = &c64;
      }
      else if(strncasecmp(optarg, "c128", 4) == 0) {
	machine = &c128;
      }
      else {
	logger->error("unknown machine type: %s", optarg);
	return false;
      }
      break;
      
    case 'm':
      self->memory = strtol(optarg, NULL, 0);
      break;

    case 'b':
      self->bank = strtol(optarg, NULL, 0);
      break;

    case 'a':
      self->start = strtol(optarg, NULL, 0);

      if ((end = strstr(optarg, "-")) != NULL) {
        self->end = strtol(end+1, NULL, 0);
      }

      if (!valid(self->start)) {
        logger->error("start address out of range: 0x%04X", self->start);
        return false;
      }

      if(self->end != -1) {
        
        if (!valid(self->end)) {
          logger->error("end address out of range: 0x%04X", self->end);
          return false;
        }
	
        if (self->end < self->start) {
          logger->error("end address before start address: 0x%04X > 0x%04X", self->end, self->start);
          return false;
        }
	
        if (self->start == self->end) {
          logger->error("start address equals end address: 0x%04X == 0x%04X", self->end, self->start);
          return false;	
        }
      }
      break;

    case 's':
      self->skip = strtol(optarg, NULL, 0);
      break;

    case 'f':
      self->force = true;
      break;
    }    
  }

  self->argc -= optind;
  self->argv += optind;
  self->offset = optind;
  return true;
}

//------------------------------------------------------------------------------

char* command_get_name(Command* self) {
  return id2str(self->id);
}

//------------------------------------------------------------------------------

bool command_print(Command* self) {

  char result[1024] = "";
  bool print = false;

  if(strlen(xlink_get_device()) > 0) {
    sprintf(result, "-d %s ",  xlink_get_device());
    print = true;
  }

  if(machine != NULL) {
    sprintf(result + strlen(result), "-M %s ", machine->name);
  }
  
  if((unsigned char) self->memory != 0xff) {
    sprintf(result + strlen(result), "-m 0x%02X ", (unsigned char) self->memory);
    print = true;
  }

  if((unsigned char) self->bank != 0xff) {
    sprintf(result + strlen(result), "-b 0x%02X ", (unsigned char) self->bank);
    print = true;
  }

  if((unsigned short) self->start != 0xffff) {
    sprintf(result + strlen(result), "-a 0x%04X", (unsigned short) self->start);

    if((unsigned short) self->end != 0xffff) {
      sprintf(result + strlen(result), "-0x%04X", (unsigned short) self->end);
    }
    sprintf(result + strlen(result), " ");
    print = true;
  }  

  if((unsigned short) self->skip != 0xffff) {
    sprintf(result + strlen(result), "-s 0x%04X ", (unsigned short) self->skip);
    print = true;
  }

  int i;
  for (i=0; i<self->argc; i++) {
    sprintf(result + strlen(result), "%s ", self->argv[i]);
    print = true;
  }

  if (print) {
    logger->debug(result);
  }

  return true;
} 

//------------------------------------------------------------------------------

bool command_find_basic_program(Command* self) {

  ushort bstart = 0x0000;
  ushort bend   = 0x0000;
  uchar value;

  if(xlink_peek(machine->memory, machine->bank, machine->basic_start+1, &value)) {
    bstart |= value;
    bstart <<= 8;
  } 
  else return false;

  if(xlink_peek(machine->memory, machine->bank, machine->basic_start, &value)) {
    bstart |= value;
  } 
  else return false;

  if(xlink_peek(machine->memory, machine->bank, machine->basic_end+1, &value)) {
    bend |= value;
    bend <<= 8;
  } 
  else return false;

  if(xlink_peek(machine->memory, machine->bank, machine->basic_end, &value)) {
    bend |= value;
  } 
  else return false;
  
  if(bend != bstart + 2) {
    self->start = bstart;
    self->end = bend;
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------

void command_apply_memory_and_bank(Command* self) {
  if (self->memory == 0xff)
    self->memory = machine->memory;

  if (self->bank == 0xff)
    self->bank = machine->bank;
}

void command_apply_safe_memory_and_bank(Command* self) {
  self->memory = machine->safe_memory;
  self->bank   = machine->safe_bank;
}

//------------------------------------------------------------------------------

bool command_none(Command* self) {

  StringList *arguments = stringlist_new();
  Commands *commands;
  bool result = true;

  command_print(self);
  
  if (self->argc > 0) {

    stringlist_append(arguments, "ready");

    for (int i=0; i<self->argc; i++) {

      if (access(self->argv[i], R_OK) == 0) {               
        stringlist_append(arguments, (i < self->argc-1) ? "load" : "run");      
        stringlist_append(arguments, self->argv[i]);      
      }
      else {
        logger->error("Unknown command: %s", self->argv[i]);
        result = false;
        goto done;
      }
    }
    
    commands = commands_new(arguments->size, arguments->strings);
    
    result = commands_execute(commands);
    
    commands_free(commands);
  }

 done:
  stringlist_free(arguments);
  return result;
}

//------------------------------------------------------------------------------

bool command_load(Command* self) {
  
  FILE *file;
  struct stat st;
  long size;
  int loadAddress;
  unsigned char *data;
  
  if (self->argc == 0) {
    logger->error("no file specified");
    return false;
  }

  char *filename = self->argv[0];
  
  file = fopen(filename, "rb");
  
  if (file == NULL) {
    logger->error("'%s': %s", filename, strerror(errno));
    return false;
  }
  stat(filename, &st);
  size = st.st_size;
  
  if (self->start == -1) {
    // no load address specified, assume PRG file
    fread(&loadAddress, sizeof(char), 2, file);
    self->start = loadAddress & 0xffff;      

    if (self->skip == -1)
      self->skip = 2;
  }
  
  if (self->skip == -1)
    self->skip = 0;

  size -= self->skip;

  if(self->end == -1) {
    self->end = self->start + size;
  }

  if(self->end - self->start < size) {
    size = self->end - self->start;
  }

  if(self->memory == 0xff || self->bank == 0xff) {

    Range* io = range_new_from_int(machine->io);
    Range* data = range_new(self->start, self->end);

    if(range_overlaps(data, io) && self->memory == 0xff)
      command_apply_safe_memory_and_bank(self);
    else 
      command_apply_memory_and_bank(self);

    free(io);
    free(data);
  }

  data = (unsigned char*) calloc(size, sizeof(unsigned char));
  
  fseek(file, self->skip, SEEK_SET);
  fread(data, sizeof(unsigned char), size, file);
  fclose(file);  

  command_print(self);

  if(self->force) logger->suspend();
  
  if(!self->force && !command_server_usable_after_possible_relocation(self)) {
    free(data);
    return false;
  }      

  if(self->force) logger->resume();
  
  if (!xlink_load(self->memory, self->bank, self->start, data, size)) {
    free(data);
    return false;
  }

  free(data);
  return true;
}

//------------------------------------------------------------------------------

bool command_save(Command* self) {
  
  FILE *file;
  char *suffix;
  int size;
  unsigned char *data;

  if (self->argc == 0) {
    logger->error("no file specified");
    return false;
  }

  char *filename = self->argv[0];

  if(self->start == -1) {
    if(!command_find_basic_program(self)) {
      logger->error("no start address specified and no basic program in memory");
      return false;
    }
  }

  if(self->start == -1) {                   
    logger->error("no start address specified");
    return false;
  }
  else {
    if(self->end == -1) {                   
      logger->error("no end address specified");
      return false;
    }
  }

  size = self->end - self->start;

  suffix = (filename + strlen(filename)-4);

  data = (unsigned char*) calloc(size, sizeof(unsigned char));

  file = fopen(filename, "wb");

  if(file == NULL) {
    logger->error("'%s': %s", filename, strerror(errno));
    free(data);
    return false;
  }

  command_apply_memory_and_bank(self);
  
  command_print(self);

  if(!xlink_save(self->memory, self->bank, self->start, data, size)) {
    free(data);
    fclose(file);
    return false;
  }

  if (strncasecmp(suffix, ".prg", 4) == 0)
    fwrite(&self->start, sizeof(unsigned char), 2, file);
  
  fwrite(data, sizeof(unsigned char), size, file);
  fclose(file);

  free(data);    
  return true;
}

//------------------------------------------------------------------------------

bool command_poke(Command* self) {
  char *argument;
  unsigned char value;
  
  if (self->argc == 0) {
    logger->error("argument required");
    return false;
  }
  argument = self->argv[0];
  unsigned int comma = strcspn(argument, ",");

  if (comma == strlen(argument) || comma == strlen(argument)-1) {
    logger->error("expects <address>,<value>");
    return false;
  }
  
  char* addr = argument;
  char* val = argument + comma + 1;
  addr[comma] = '\0';

  self->start = strtol(addr, NULL, 0);
  value = strtol(val, NULL, 0);

  command_apply_memory_and_bank(self);

  self->end = self->start;
  
  command_print(self);

  if(!command_server_usable_after_possible_relocation(self)) {
    return false;
  }      

  return xlink_poke(self->memory, self->bank, self->start, value);
}

//------------------------------------------------------------------------------

bool command_peek(Command* self) {
  
  if (self->argc == 0) {
    logger->error("no address specified");
    return false;
  }

  int address = strtol(self->argv[0], NULL, 0);
  unsigned char value;

  command_apply_memory_and_bank(self);

  command_print(self);

  if(!xlink_peek(self->memory, self->bank, address, &value)) {
    return false;
  }
  printf("%d\n", value);
  
  return true;
}

//------------------------------------------------------------------------------

bool command_fill(Command* self) {

  bool result = false;

  if (self->argc == 0) {
    logger->error("no arguments given");
    goto done;
  }

  if(self->argc == 1) {
    logger->error("no value specified");
    goto done;
  }

  Range *range = range_parse(self->argv[0]);

  if(!range_ends(range)) {
    range->end = 0x10000;
  }

  if(!range_valid(range)) {
    logger->error("invalid memory range: $%04X-$%04X", range->start, range->end);
    free(range);
    goto done;
  }
  
  unsigned char value = (unsigned char) strtol(self->argv[1], NULL, 0);


  int size = range_size(range);
  
  self->start = range->start;
  self->end = range->end;
  
  free(range);

  command_apply_memory_and_bank(self);
  
  command_print(self);

  if(!command_server_usable_after_possible_relocation(self)) {
    goto done;
  }      

  result = xlink_fill(self->memory, self->bank, self->start, value, size);  
  
 done:
  return result;
}

//------------------------------------------------------------------------------

bool command_jump(Command* self) {

  if (self->argc == 0) {
    logger->error("no address specified");
    return false;
  }

  int address = strtol(self->argv[0], NULL, 0);

  if(address == 0) {
    if(self->start != -1) {
      address = self->start;
    }
    else {
      logger->error("no address specified");
      return false;    
    }
  }
  command_apply_memory_and_bank(self);

  command_print(self);

  return xlink_jump(self->memory, self->bank, address);
}

//------------------------------------------------------------------------------

bool command_run(Command* self) {
  bool result = false;

  if(self->argc == 1) {

    logger->suspend();
    if(!(result = command_load(self))) {
      logger->resume();
      return result;
    }
    logger->resume();

    if (self->start != machine->default_basic_start) {

      command_apply_memory_and_bank(self);
      
      command_print(self);

      return xlink_jump(self->memory, self->bank, self->start);
    }
  }
  command_print(self);
  return xlink_run();
}

//------------------------------------------------------------------------------

extern bool xlink_relocate(unsigned short address);

bool command_server_usable_after_possible_relocation(Command* self) {

  unsigned short newServerAddress;  
  xlink_server_info_t server;
  
  if(xlink_identify(&server)) {

    if(command_requires_server_relocation(self, &server)) {

      if(!command_server_relocation_possible(self, &server, &newServerAddress)) {
        logger->error("impossible to relocate ram-based server: out of memory");
        return false;
      }

      logger->debug("relocating server to $%04X", newServerAddress);

      if(!xlink_relocate(newServerAddress)) {
        logger->error("failed to relocate ram-based server: %s", xlink_error->message);
        return false;
      }
    }
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------

bool command_requires_server_relocation(Command* self, xlink_server_info_t* server) {

  bool result = false;

  if(server->type == XLINK_SERVER_TYPE_ROM) {
    return false;
  }

  Range* data = range_new(self->start, self->end);
  Range* code = range_new(server->start, server->end);

  if(range_overlaps(data, code)) {
    
    logger->debug("relocation required: data ($%04X-$%04X) overlaps server ($%04X-$%04X)",
		  self->start, self->end, server->start, server->end);
    
    result = true;
  }
  free(data);
  free(code);
  return result;  
}

//------------------------------------------------------------------------------

bool command_server_relocation_possible(Command* self, xlink_server_info_t* server, unsigned short* address) {

  bool result = true;

  Range* data = range_new(self->start, self->end);
  Range* code = range_new(server->start, server->end);
  
  Range *screen = range_new_from_int(machine->screenram);
  Range *upper  = range_new_from_int(machine->loram);
  Range *lower  = range_new_from_int(machine->hiram);

  if(machine->type == XLINK_MACHINE_C64) {
    lower->end = server->memtop;
  }
  
  // try to relocate server as close as possible to...

  // ...the end of the upper memory area
  
  code->start = upper->end - server->length;
  code->end = upper->end;
  
  while(range_inside(code, upper)) {

    if(range_overlaps(code, data)) {
      range_move(code, -1);
    }
    else {
      (*address) = code->start;
      goto done;
    }
  }

  // ...the end of the lower memory area
  
  code->start = lower->end - server->length;
  code->end = lower->end;

  while(range_inside(code, lower)) {
    
    if(range_overlaps(code, data)) {
      range_move(code, -1);
    }
    else {
      (*address) = code->start;
      goto done;
    }
  }

  // ...the end of the default screen memory area (last resort)

  code->start = screen->end - server->length;
  code->end = screen->end;

  while(range_inside(code, screen)) {
    
    if(range_overlaps(code, data)) {
      range_move(code, -1);
    }
    else {
      (*address) = code->start;
      goto done;
    }
  }
  
  result = false;
  
 done:  
  free(code);
  free(data);
  free(upper);
  free(lower);
  free(screen);
  return result;  
}

//------------------------------------------------------------------------------

bool command_relocate(Command *self) {

  bool result = false;
  
  xlink_server_info_t server;

  if(self->argc != 1) {
    logger->error("no relocation address specified");
    return false;
  }
  
  if(!xlink_identify(&server)) {
    logger->error("failed to identify server");
    return false;
  }

  if(server.type == XLINK_SERVER_TYPE_ROM) {
    logger->info("identified ROM-based server (no relocation required)");
    return true;
  }

  unsigned short address = strtol(self->argv[0], NULL, 0);

  Range* code  = range_new(address, address + server.length);

  Range* io = range_new_from_int(machine->io);    
  Range* lorom = range_new_from_int(machine->lorom);
  Range* hirom = range_new_from_int(machine->hirom);

  if(machine->type == XLINK_MACHINE_C64) {
    lorom->start = server.memtop;
  }

  if(!range_valid(code)) {
    logger->error("cannot relocate server to $%04X-$%04X: invalid memory range",
		  code->start, code->end);
    goto done;
  }
  
  if(server.type == XLINK_SERVER_TYPE_RAM) {
   
    if(range_inside(code, lorom)) {
      logger->error("cannot relocate server to $%04X-$%04X: range occupies lower rom area $%04X-$%04X",
		    code->start, code->end, lorom->start, lorom->end);
      goto done;
    }

    if(range_inside(code, hirom)) {
      logger->error("cannot relocate server to $%04X-$%04X: range occupies upper rom area $%04X-$%04X",
		    code->start, code->end, hirom->start, hirom->end);
      goto done;
    }

    if(range_inside(code, io)) {
      logger->error("cannot relocate server to $%04X-$%04X: range occupies io area $%04X-$%04X",
		    code->start, code->end, io->start, io->end);
      goto done;
    }

    result = xlink_relocate(address);
    goto done;
  }

  logger->error("unknown server type: %d", server.type);
  
 done:
    free(lorom);
    free(hirom);
    free(io);
    free(code);

  return result;
}

//------------------------------------------------------------------------------

static bool server_ready_after(int ms) {

  while(ms) {
    if(xlink_ping()) {
      usleep(250*1000);
      return true;
    }
    ms-=250;
  }
  return false;
}

//------------------------------------------------------------------------------

bool command_reset(Command* self) {

  bool result = false;
  int timeout = 3000;
  
  command_print(self);

  if(xlink_reset()) {
    if(server_ready_after(timeout)) {
      result = xlink_ready();
    }
  }
  return result;
}

//------------------------------------------------------------------------------

extern bool xlink_bootloader(void);

int command_bootloader(Command *self) {
  command_print(self);
  return xlink_bootloader();
}

//------------------------------------------------------------------------------

bool command_benchmark(Command* self) {

  Watch* watch = watch_new();
  bool result = false;
  xlink_server_info_t server;
  
  Range *benchmark;

  if(self->start != -1 && self->end != -1) {
    benchmark = range_new(self->start, self->end);
  }
  else {
    benchmark = range_new_from_int(machine->benchmark);
  }

  command_apply_memory_and_bank(self);

  command_print(self);
  
  unsigned char payload[range_size(benchmark)];
  unsigned char roundtrip[sizeof(payload)];

  srand(time(0));
  for(int i=0; i<sizeof(payload); i++) {
    payload[i] = (unsigned char) rand();
  }
  
  int start = benchmark->start;
    
  if (!xlink_ping()) {
    logger->error("no response from server");
    goto done;
  }

  if(xlink_identify(&server)) {
    if(server.type == XLINK_SERVER_TYPE_RAM) {
      xlink_relocate(machine->free_ram_area);
      usleep(250*1000);
    }
  }
  
  logger->info("sending %d bytes...", sizeof(payload));
    
  watch_start(watch);

  if(!xlink_load(self->memory, self->bank, start, payload, sizeof(payload))) goto done;
  
  float seconds = (watch_elapsed(watch) / 1000.0);
  float kbs = sizeof(payload)/seconds/1024;
    
  logger->info("%.2f seconds at %.2f kb/s", seconds, kbs);       
    
  logger->info("receiving %d bytes...", sizeof(payload));
    
  watch_start(watch);

  if(!xlink_save(self->memory, self->bank, start, roundtrip, sizeof(roundtrip))) goto done;
  
  seconds = (watch_elapsed(watch) / 1000.0);
  kbs = sizeof(payload)/seconds/1024;
    
  logger->info("%.2f seconds at %.2f kb/s", seconds, kbs);
    
  logger->info("verifying...");
  
  for(int i=0; i<sizeof(payload); i++) {
    if(payload[i] != roundtrip[i]) {
      logger->error("roundtrip error at $%04X: sent %d, received %d", start+i, payload[i], roundtrip[i]);
      result = false;
      goto done;
    }
  }
  logger->info("completed successfully");
  
  result = true;
  
 done:
  range_free(benchmark);
  watch_free(watch);
  return result;
}

//------------------------------------------------------------------------------

bool command_identify(Command *self) {

  xlink_server_info_t server;
  
  if(xlink_identify(&server)) {

    printf("%s %d.%d %s %s $%04X-$%04X\n",
           server.id,
           (server.version & 0xf0) >> 4, server.version & 0x0f,
           server.machine == XLINK_MACHINE_C64 ? "C64" : (XLINK_MACHINE_C128 ? "C128" : "Unknown"),
           server.type == XLINK_SERVER_TYPE_RAM ? "RAM" : "ROM",
           server.start, server.end);

    return true;
  }
  return false;
}

//------------------------------------------------------------------------------

bool command_server(Command *self) {

  bool result = false;

  FILE *file;
  int size;
  unsigned char *data;

  if (self->argc == 0) {
    logger->error("no file specified");
    return false;
  }
  if (self->start == -1) {
    self->start = machine->default_basic_start;
  }

  command_print(self);
  
  if(self->start == machine->default_basic_start) {
    data = machine->basic_server(&size);
  } else {
    data = machine->server(self->start, &size);

    if(data == NULL) {
      return false;
    }    
  }

  if ((file = fopen(self->argv[0], "wb")) == NULL) {
    logger->error("couldn't open %s for writing: %s", strerror(errno));
    goto done;
  }

  fwrite(data, sizeof(unsigned char), size, file);
  fclose(file);

  logger->info("wrote %s (%d bytes)", self->argv[0], size);

  result = true;

 done:
  free(data);
  return result;
}

//------------------------------------------------------------------------------

bool command_kernal(Command *self) {

  bool result = false;
  struct stat st;
  int size;
  int offset;
  FILE *file;
  
  if(self->argc < 1) {
    logger->error("no input file specified");
    goto done;
  }

  if(self->argc < 2) {
    logger->error("no output file specified");
    goto done;
  }

  char *inputfile = self->argv[0];
  char *outputfile = self->argv[1];

  if(stat(inputfile, &st) == -1) {
    logger->error("%s: %s", inputfile, strerror(errno));
    goto done;
  }

  size = st.st_size;
  offset = size - 0x2000;
  
  if(offset < 0) {
    logger->error("input file: size must be larger than %d bytes (%s: %d bytes)",
		  0x2000, inputfile, size);
    goto done;
  }

  if((file = fopen(inputfile, "rb")) == NULL) {
    logger->error("%s: %s\n", inputfile, strerror(errno));
    goto done;
  }

  unsigned char *image = (unsigned char*) calloc(size, sizeof(unsigned char));  
  fread(image, sizeof(unsigned char), size, file);
  fclose(file);

  machine->kernal(image+offset);

  if((file = fopen(outputfile, "wb+")) == NULL) {
    logger->error("%s: %s\n", outputfile, strerror(errno));
    free(image);
    goto done;
  }

  fwrite(image, sizeof(unsigned char), size, file);
  fclose(file);
  
  logger->info("patched %s", outputfile);
  
  result = true;

  free(image);
  
 done:  
  return result;
}

//------------------------------------------------------------------------------

bool command_help(Command *self) {

  if (self->argc > 0) {
    logger->error("unknown command: %s", self->argv[0]);
    return false;
  }

  mode = MODE_HELP;
  return true;
}

//------------------------------------------------------------------------------

bool command_ready(Command* self) {

  command_print(self);

  if (!xlink_ready()) {
    logger->error("no response from server");
    return false;
  }
  return true;
}

//------------------------------------------------------------------------------

bool command_ping(Command* self) {
  command_print(self);
  
  Watch *watch = watch_new();

  bool response = xlink_ping();

  if (response) {
    logger->info("received reply after %.0fms", watch_elapsed(watch));
  } 
  else {
    logger->info("no reply after %.0fms", watch_elapsed(watch));
  }
  watch_free(watch);
  return response;
}

//------------------------------------------------------------------------------

bool command_execute(Command* self) {

  bool result = false;

  if(mode == MODE_HELP) {
    return help(self->id);
  }

  logger->enter(command_get_name(self));

  if(!(result = command_parse_options(self))) {
    logger->leave();
    return result;
  }

  switch(self->id) {

  case COMMAND_NONE       : result = command_none(self);       break;
  case COMMAND_LOAD       : result = command_load(self);       break;
  case COMMAND_SAVE       : result = command_save(self);       break;
  case COMMAND_POKE       : result = command_poke(self);       break;
  case COMMAND_PEEK       : result = command_peek(self);       break;
  case COMMAND_JUMP       : result = command_jump(self);       break;
  case COMMAND_RUN        : result = command_run(self);        break;
  case COMMAND_RESET      : result = command_reset(self);      break;
  case COMMAND_HELP       : result = command_help(self);       break;
  case COMMAND_READY      : result = command_ready(self);      break;
  case COMMAND_PING       : result = command_ping(self);       break;
  case COMMAND_BOOTLOADER : result = command_bootloader(self); break;
  case COMMAND_BENCHMARK  : result = command_benchmark(self);  break;
  case COMMAND_IDENTIFY   : result = command_identify(self);   break;
  case COMMAND_SERVER     : result = command_server(self);     break;
  case COMMAND_RELOCATE   : result = command_relocate(self);   break;
  case COMMAND_KERNAL     : result = command_kernal(self);     break;            
  case COMMAND_FILL       : result = command_fill(self);       break;            
  }
  
  logger->leave();

  return result;
}

//------------------------------------------------------------------------------

int main(int argc, char **argv) {

  Commands *commands;
  int result;

  logger->set("INFO");
  logger->enter(argv[0]);

  argc--; argv++;

  if (argc == 0) {
    usage();
    return EXIT_FAILURE;
  }
  
  if(argc == 1) {
    if (strcmp(argv[0], "help") == 0) {
      usage();
      return EXIT_SUCCESS;
    } 
  }

  commands = commands_new(argc, argv);

  result = commands_execute(commands) ? EXIT_SUCCESS : EXIT_FAILURE;

  commands_free(commands);

  logger->leave();

  return result;
}

//------------------------------------------------------------------------------

void version(void) {
  printf("xlink %.1f Copyright (C) 2015 Henning Bekel <h.bekel@googlemail.com>\n", CLIENT_VERSION);
}

//------------------------------------------------------------------------------

void usage(void) {
  version();
  printf("\n");
  printf("Usage: xlink [<opts>] [<command> [<opts>] [<arguments>]]...\n");
  printf("\n");
  printf("Options:\n");
  printf("    -h, --help                    : show this help\n");
  printf("    -q, --quiet                   : show errors only\n");
  printf("    -v, --verbose                 : show verbose debug output\n");
#if linux
  printf("    -d, --device <path>           : ");
  printf("transfer device (default: /dev/xlink)\n");
#elif windows
  printf("    -d, --device <port or \"usb\">  : ");
  printf("transfer device (default: \"usb\")\n");
#endif
  printf("    -M, --machine                 : machine type (default: C64)\n");
  printf("    -m, --memory                  : C64/C128 memory config (default: 0x37/0x00)\n");
  printf("    -b, --bank                    : C128 bank value (default: 15)\n");
  printf("    -a, --address <start>[-<end>] : address/range (default: autodetect)\n");
  printf("    -s, --skip <n>                : Skip n bytes of file\n");
  printf("\n");
  printf("Commands:\n");
  printf("     help  [<command>]            : show detailed help for command\n");
  printf("\n");
  printf("     kernal <infile> <outfile>    : patch kernal image to include server code\n");
  printf("     server [-a<addr>] <file>     : create server program and save to file\n");
  printf("     relocate <addr>              : relocate currently running server\n");
  printf("\n");  
  printf("     reset                        : reset machine (requires hardware support)\n");
  printf("     ready                        : try to make sure the server is ready\n");
  printf("     ping                         : check if the server is available\n");
  printf("     identify                     : identify remote server and machine type\n");
  printf("\n");
  printf("     load  [<opts>] <file>        : load file into memory\n");
  printf("     save  [<opts>] <file>        : save memory to file\n");
  printf("     poke  [<opts>] <addr>,<val>  : poke value into memory\n");
  printf("     peek  [<opts>] <addr>        : read value from memory\n");
  printf("     fill  <range>  <val>         : fill memory range with value\n");
  printf("     jump  [<opts>] <addr>        : jump to specified address\n");
  printf("     run   [<opts>] [<file>]      : run program, optionally load it before\n");
  printf("     <file>...                    : load file(s) and run last file\n");
  printf("\n");
  printf("     benchmark [<opts>]           : test/measure transfer speed\n");
  printf("     bootloader                   : enter dfu-bootloader (at90usb162)\n");  
  printf("\n");
}

//------------------------------------------------------------------------------

#include "help.c"

//------------------------------------------------------------------------------
