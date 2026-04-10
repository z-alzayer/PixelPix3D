FROM devkitpro/devkitarm:latest

# Install 3DS libraries and build tools
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        wget ca-certificates unzip git build-essential cmake \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h \
    && wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h

RUN git clone --depth 1 --branch makerom-v0.19.0 https://github.com/3DSGuy/Project_CTR.git /tmp/project_ctr

RUN make -C /tmp/project_ctr/makerom deps

RUN make -C /tmp/project_ctr/makerom

RUN mv /tmp/project_ctr/makerom/bin/makerom /usr/local/bin/makerom \
    && rm -rf /tmp/project_ctr

# Build and install bannertool
RUN git clone --depth 1 https://github.com/carstene1ns/3ds-bannertool /tmp/3ds-bannertool \
    && cmake -B /tmp/3ds-bannertool/build -S /tmp/3ds-bannertool -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/3ds-bannertool/build \
    && mv /tmp/3ds-bannertool/build/bannertool /usr/local/bin/bannertool \
    && rm -rf /tmp/3ds-bannertool

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
