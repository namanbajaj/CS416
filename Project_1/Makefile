%: %.c
	@if [ "$@" = "threads" ]; then \
		gcc -pthread $< -o $@; \
	else \
		gcc $< -o $@; \
	fi