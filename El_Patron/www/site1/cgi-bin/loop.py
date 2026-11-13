# Demo: a script that never ends. webserv stops it after 10 seconds without
# activity and answers 504 Gateway Timeout; meanwhile, every other page
# keeps loading: the server never waits for a script.
while True:
    pass
