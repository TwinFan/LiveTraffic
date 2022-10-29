platforms=win mac lin lin-bionic
container_focal=twinfan/focal-win-mac-lin-compile-env:02.00
container_bionic=twinfan/bionic-lin-compile-env:01.00

all: doc $(platforms)

.image_bionic:
	docker image pull $(container_bionic)
	docker inspect -f "{{ .ID}}" $(container_bionic) > .image_bionic

.image_focal:
	docker image pull $(container_focal)
	docker inspect -f "{{ .ID}}" $(container_focal) > .image_focal

bash_bionic: .image_bionic
	docker run -it --rm -v "$(realpath ..):/build" --entrypoint bash $(container_bionic)

bash_focal: .image_focal
	docker run -it --rm -v "$(realpath ..):/build" --entrypoint bash $(container_focal)

lin-bionic: .image_bionic
	docker run -i --rm -v "$(realpath ..):/build" --entrypoint docker/build.sh $(container_bionic) $@

lin win mac-arm mac-x86: .image_focal
	docker run -i --rm -v "$(realpath ..):/build" --entrypoint docker/build.sh $(container_focal) $@

mac: mac-arm mac-x86
	docker run -i --rm -v "$(realpath ..):/build" --entrypoint bash $(container_focal) -c 'mkdir -p build-mac/mac_x64 ; lipo -create -output build-mac/mac_x64/LiveTraffic.xpl build-mac-x86/mac_x64/LiveTraffic.xpl build-mac-arm/mac_x64/LiveTraffic.xpl'

doc:
	cd .. && /Applications/Doxygen.app/Contents/Resources/doxygen docs/LiveTraffic.doxygen

clean:
	rm -rf ../build-lin
	rm -rf ../build-lin-bionic
	rm -rf ../build-mac
	rm -rf ../build-mac-arm
	rm -rf ../build-mac-x86
	rm -rf ../build-win

.PHONY: doc $(platforms) bash_bionic bash_focal
