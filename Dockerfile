FROM devkitpro/devkitarm:latest

# Install 3DS libraries and build tools
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        wget ca-certificates unzip git build-essential \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h \
    && wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h

RUN git clone --depth 1 --branch makerom-v0.19.0 https://github.com/3DSGuy/Project_CTR.git /tmp/project_ctr

RUN make -C /tmp/project_ctr/makerom deps

RUN make -C /tmp/project_ctr/makerom

RUN mv /tmp/project_ctr/makerom/bin/makerom /usr/local/bin/makerom \
    && rm -rf /tmp/project_ctr

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
