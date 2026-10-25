# Builds the standalone config test driver (tests/config/config_test).
# Usage from the repository root: make -C tests/config

NAME		= config_test

ROOT		= ../..
D_SRCS		= $(ROOT)/srcs/config
D_OBJS		= .build

S_SRCS		= Token.cpp ConfigError.cpp ConfigFile.cpp Tokenizer.cpp
SRCS		= $(addprefix $(D_SRCS)/, $(S_SRCS))
OBJS		= $(addprefix $(D_OBJS)/, $(S_SRCS:.cpp=.o)) $(D_OBJS)/config_test.o
HEADERS		= $(wildcard $(ROOT)/includes/config/*.hpp)

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98
IFLAGS		= -I$(ROOT)/includes
RM			= rm -rf

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(D_OBJS)/%.o: $(D_SRCS)/%.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(IFLAGS) -c $< -o $@

$(D_OBJS)/config_test.o: config_test.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(IFLAGS) -c $< -o $@

clean:
	$(RM) $(D_OBJS)

fclean: clean
	$(RM) $(NAME)

re: fclean all

.PHONY: all clean fclean re
