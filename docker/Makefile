platforms=win mac lin
container=lt-cross-compile-env

# all: mac lin win
all: lin win 

.image: Dockerfile build.sh
	docker build . -t $(container)
	docker inspect -f "{{ .ID}}" $(container):latest > .image

bash: .image
	docker run -it --rm -v "$(realpath ../Src):/Src" --entrypoint bash $(container):latest

$(platforms): .image
	docker run -i --rm -v "$(realpath ../Src):/Src" $(container):latest $@

.PHONY: $(platforms) bash
