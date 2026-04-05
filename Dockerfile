FROM devkitpro/devkitarm:latest

# Install 3DS libraries and build tools
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        wget ca-certificates unzip \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h \
    && wget -q -O /opt/devkitpro/devkitARM/arm-none-eabi/include/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h

RUN wget -q https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip \
    && unzip makerom-v0.19.0-ubuntu_x86_64.zip \
    && mv makerom /usr/local/bin/makerom \
    && chmod +x /usr/local/bin/makerom \
    && rm makerom-v0.19.0-ubuntu_x86_64.zip

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
