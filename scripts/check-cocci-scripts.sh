#! /usr/bin/env bash

test -d scripts/coccinelle || exit 1

SPATCH_IMAGE="philmd/coccinelle:1.0.4"

GIT_AUTHOR_NAME="Coccinelle Spatch"
GIT_COMMITTER_NAME="Coccinelle Spatch"

if [ -n "$TRAVIS" ]; then
	# avoid stalling builds: https://docs.travis-ci.com/user/common-build-problems/#Build-times-out-because-no-output-was-received
	TIMEOUT_S=530
	TIMEOUT_CMD="timeout -k 550 500"
	EXTRA_ARGS="--timeout ${TIMEOUT_S}"
else
	TIMEOUT_S=0
fi

HEAD=19 #`echo -n scripts/coccinelle/ | wc -c`
TAIL=6 #`echo -n .cocci | wc -c`

test -z "$(${SUDO} docker images -q ${SPATCH_IMAGE})" && ${SUDO} docker pull ${SPATCH_IMAGE}

LOG=/tmp/cocci-spatch-$$
for script in scripts/coccinelle/*.cocci; do
	desc=${script:$HEAD:-$TAIL}
	echo -e "\nRunning ${script}...\n"
	echo -e "coccinelle: committing changes after running \"$desc\" script\n" > ${LOG}.topic
	${TIMEOUT_CMD} ${SUDO} \
	docker run --rm -v `pwd`:`pwd` -w `pwd` -u `id -u` \
		${SPATCH_IMAGE} --use-cache --use-gitgrep --keep-comments \
			--very-quiet ${EXTRA_ARGS} \
			--sp-file ${script} \
			--macro-file scripts/cocci-macro-file.h \
			--dir . \
			--in-place | tee ${LOG}.content
	git add -u
	git diff --cached --exit-code -s
	if [ $? -ne 0 ]; then
		:> ${LOG}.content
	else
		test -s ${LOG}.content || continue
	fi
	cat ${LOG}.{topic,content} | git commit --allow-empty -F -
done

rm -f ${LOG}.{topic,content}
