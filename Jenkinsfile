def notify(status){
	emailext (
		body: '$DEFAULT_CONTENT',
		recipientProviders: [
			[$class: 'CulpritsRecipientProvider'],
			[$class: 'DevelopersRecipientProvider'],
			[$class: 'RequesterRecipientProvider']
		],
		replyTo: '$DEFAULT_REPLYTO',
		subject: '$DEFAULT_SUBJECT',
		to: '$DEFAULT_RECIPIENTS'
	)
}

@NonCPS
def killall_jobs() {
	def jobname = env.JOB_NAME
	def buildnum = env.BUILD_NUMBER.toInteger()
	def killnums = ""
	def job = Jenkins.instance.getItemByFullName(jobname)
	def fixed_job_name = env.JOB_NAME.replace('%2F','/')

	for (build in job.builds) {
		if (!build.isBuilding()) { continue; }
		if (buildnum == build.getNumber().toInteger()) { continue; println "equals" }
		if (buildnum < build.getNumber().toInteger()) { continue; println "newer" }

		echo "Kill task = ${build}"

		killnums += "#" + build.getNumber().toInteger() + ", "

		build.doStop();
	}

	if (killnums != "") {
		slackSend color: "danger", channel: "#jenkins", message: "Killing task(s) ${fixed_job_name} ${killnums} in favor of #${buildnum}, ignore following failed builds for ${killnums}"
	}
	echo "Done killing"
}

def buildStep(dockerImage, generator, os, defines, arch) {
	def split_job_name = env.JOB_NAME.split(/\/{1}/)
	def fixed_job_name = split_job_name[1].replace('%2F',' ')
    def fixed_os = os.replace(' ','-')
	try{
		stage("Building on \"${dockerImage}\" with \"${generator}\" for \"${os}\"...") {
			properties([pipelineTriggers([githubPush()])])
			def commondir = env.WORKSPACE + '/../' + fixed_job_name + '/'

			def pathInContainer

			def dockerImageRef = docker.image("${dockerImage}")
			dockerImageRef.pull()

			dockerImageRef.inside("") {
				pathInContainer = steps.sh(script: 'echo $PATH', returnStdout: true).trim()
			}

			dockerImageRef.inside("-e HOME='/tmp' -e PATH=${env.WORKSPACE}/dependencies/depot_tools/:${pathInContainer} --privileged") {
				checkout scm

				if (env.CHANGE_ID) {
					echo 'Trying to build pull request'
				}

				if (!env.CHANGE_ID) {
					sh "rm -rfv publishing/deploy/*"
					sh "mkdir -p publishing/deploy/gs2emu"
				}

				dir("dependencies") {
					sh "BUILDARCH=${arch} ./build-v8-linux"
				}

				sh "mkdir -p build/"
				sh "mkdir -p lib/"
				sh "sudo rm -rfv build/*"

				slackSend color: "good", channel: "#jenkins", message: "Starting ${os} build target..."
				dir("build") {
					sh "export PATH=${env.WORKSPACE}/dependencies/depot_tools/:${pathInContainer}"
					sh "cmake -G\"${generator}\" ${defines} -DVER_EXTRA=\"-${fixed_os}-${fixed_job_name}\" .."
					sh "cmake --build . --config Release --target package -- -j 8"
					//sh "cmake --build . --config Release --target package_source -- -j 8"

					archiveArtifacts artifacts: '*.zip,*.tar.gz,*.tgz'
				}

				slackSend color: "good", channel: "#jenkins", message: "Build ${fixed_job_name} #${env.BUILD_NUMBER} Target: ${os} DockerImage: ${dockerImage} Generator: ${generator} successful!"
			}
		}
	} catch(err) {
		slackSend color: "danger", channel: "#jenkins", message: "Build Failed: ${fixed_job_name} #${env.BUILD_NUMBER} Target: ${os} DockerImage: ${dockerImage} Generator: ${generator} (<${env.BUILD_URL}|Open>)"
		currentBuild.result = 'FAILURE'
		notify('Build failed')
		throw err
	}
}

node('master') {
	killall_jobs()
	def fixed_job_name = env.JOB_NAME.replace('%2F','/')
	slackSend color: "good", channel: "#jenkins", message: "Build Started: ${fixed_job_name} #${env.BUILD_NUMBER} (<${env.BUILD_URL}|Open>)"
	parallel (
		//'Win64-NPCServer': {
		//	node {
		//		buildStep('dockcross/windows-static-x64:latest', 'Unix Makefiles', 'Windows x86_64 NPCServer', "-DV8NPCSERVER=TRUE")
		//	}
		//},
		/*
		'Win64': {
			node {
				buildStep('desertbit/crossbuild:windows-x86_64', 'Unix Makefiles', 'Windows x86_64', "-DV8NPCSERVER=FALSE", 'x86_64')
			}
		},
		*/
		'Linux x86_64-NPCServer': {
			node {
				buildStep('desertbit/crossbuild:linux-x86_64', 'Unix Makefiles', 'Linux x86_64 NPCServer', '-DV8NPCSERVER=TRUE', 'x86_64')
			}
		},/*
		'Linux x86_64': {
			node {
				buildStep('desertbit/crossbuild:linux-x86_64', 'Unix Makefiles', 'Linux x86_64', "-DV8NPCSERVER=FALSE")
			}
		},*/
		'Linux ARMv7-NPCServer': {
			node {
				buildStep('desertbit/crossbuild:linux-armv7', 'Unix Makefiles', 'Linux RasPi NPCServer', '-DV8NPCSERVER=TRUE', 'arm')
			}
		}/*,
		'Linux ARMv7': {
			node {
				buildStep('desertbit/crossbuild:linux-armv7', 'Unix Makefiles', 'Linux RasPi', '-DV8NPCSERVER=FALSE')
			}
		},
		'WebASM': {
			node {
				buildStep('dockcross/web-wasm:latest', 'Unix Makefiles', 'Web assembly', "-DV8NPCSERVER=FALSE")
			}
		}*/
    )
}
