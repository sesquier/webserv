#******************************************************************************#
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/05/12 19:10:39 by ale-coss          #+#    #+#              #
#    Updated: 2026/10/25 17:59:15 by ale-coss         ###   ########.fr        #
#                                                                              #
#******************************************************************************#

# Nom de l'exécutable

NAME = webserv

# Fichiers source

D_SRCS = ./srcs
S_SRCS = main.cpp server.cpp handlers.cpp request.cpp cgi.cpp cgiEvents.cpp \
	config/Token.cpp config/ConfigError.cpp config/ConfigFile.cpp \
	config/Tokenizer.cpp config/LocationConfig.cpp config/ServerConfig.cpp \
	config/ConfigParser.cpp config/Config.cpp config/ConfigDump.cpp
SRCS = $(addprefix $(D_SRCS)/, $(S_SRCS))

# Fichiers includes

D_INC = ./includes/

# Couleurs

DEFAULT = \033[0m
WHITE = \033[1;37m
GREEN = \033[0;92m
YELLOW = \033[0;93m
CYAN = \033[0;96m
UNDERLINE = \033[4m
BOLD = \033[1m

# Fichiers objets

D_OBJS = .build
OBJS = $(addprefix $(D_OBJS)/, $(S_SRCS:.cpp=.o))
DEPS = $(OBJS:.o=.d)

# Compilateur et flags

CXX = c++ 
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 
IFLAGS = -I$(D_INC) 
RM = rm -rf

# Règle par défaut

all: $(NAME)

# Compilation de l'exécutable

$(NAME): $(OBJS) 
	@$(CXX) $(CXXFLAGS) -o $@ $(OBJS)
	@printf "%b\n" "$(YELLOW)$(BOLD)Linking: $@$(DEFAULT)"

# Compilation des .cpp en .o

$(D_OBJS)/%.o: $(D_SRCS)/%.cpp 
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) $(IFLAGS) -MMD -MP -c $< -o $@
	@printf "%b\n" "$(YELLOW)$(BOLD)Compiling: $<$(DEFAULT)"

# Recompile un .cpp quand un header qu'il inclut change

-include $(DEPS)

# Supprimer les fichiers objets

clean: 
	@$(RM) $(D_OBJS)
	@printf "%b\n" "$(CYAN)$(BOLD)Object files removed successfully!$(DEFAULT)"

# Supprimer les fichiers objets et l'exécutable

fclean: clean
	@$(RM) $(NAME)
	@printf "%b\n" "$(CYAN)$(BOLD)Executable removed successfully!$(DEFAULT)"

# Recompiler proprement

re: fclean 
	@$(MAKE) --silent all

# Précise que ces cibles ne sont pas des fichiers

.PHONY: all clean fclean re