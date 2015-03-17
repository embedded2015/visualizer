#!/usr/bin/env python

# Configure wether to trace these feature
# Warning : Too many contents may freeze Grasp
TRACE_QUEUE = True
TRACE_MUTEX = True
TRACE_BINARY_SEMAPHORE = False
TRACE_INTERRUPT = False

log = open('log', 'r')
lines = log.readlines()

tasks = {}
events = []
mutexes = {}
all_queues = {}
binsems = {}
queues = {}

for line in lines :
	line = line.strip()
	inst, args = line.split(' ', 1)
	
	if inst == 'task' :
		id, priority, name = args.split(' ', 2)
		
		task = {}
		task['no'] = str(len(tasks) + 1)
		task['priority'] = int(priority)
		task['name'] = task['no'] + ": " + name.strip()
		task['created'] = True
		
		tasks[id] = task
		
	elif inst == 'switch' :
		out_task, in_task, tick, tick_reload, out_minitick, in_minitick = args.split(' ')
		
		out_time = (int(tick) + (int(tick_reload) - int(out_minitick)) / int(tick_reload)) / 100 * 1000;
		in_time  = (int(tick) + (int(tick_reload) - int(in_minitick))  / int(tick_reload)) / 100 * 1000;
		
		event = {}
		event['type'] = 'task out'
		event['task'] = out_task
		event['time'] = out_time
		event['next'] = in_task
		events.append(event);

		event = {}
		event['type'] = 'task in'
		event['task'] = in_task
		event['time'] = in_time
		events.append(event);

		last_task = in_task

	elif inst == 'mutex' and TRACE_MUTEX :
		task, id = args.split(' ')
		mutex = {}
		mutex['type'] = 'mutex'
		mutex['name'] = 'Mutex ' + str(len(mutexes) + 1)
		time, mutex['id'] = args.split(' ')
		mutexes[id] = mutex;
		all_queues[id] = mutex;

	elif inst == 'queue' :
		act, args = args.split(' ', 1)
		if act == 'create' :
			time, id, queue_type, queue_size = args.split(' ')

			if queue_type == '0' and TRACE_QUEUE :
				queue = {}
				queue['type'] = 'queue'
				queue['name'] = 'Queue ' + str(len(queues) + 1)
				queue['size'] = queue_size
				queues[id] = queue
				all_queues[id] = queue

			if queue_type == '3' and TRACE_BINARY_SEMAPHORE :	# Binary semaphore, see FreeRTOS/queue.c
				binsem = {}
				binsem['type'] = 'binary semaphore'
				binsem['name'] = "Binary Semaphore " + str(len(binsems) + 1)
				binsems[id] = binsem;
				all_queues[id] = binsem;

		elif act == 'send' or act == 'recv' :
			time, task_id, id = args.split(' ')
			if id in all_queues and int(time) > 0 :
				queue = all_queues[id]

				event = {}
				event['target'] = id
				event['task'] = task_id
				event['time'] = float(time) / 1000

				if queue['type'] == 'mutex' :
					event['type'] = 'mutex ' + ('take' if act == 'recv' else 'give')
					queue['acquired'] = True if act == 'recv' else False
					if act == 'recv' :
						queue['last_acquire'] = last_task

				elif queue['type'] == 'binary semaphore' :
					event['type'] = 'semaphore ' + ('take' if act == 'recv' else 'give')

				elif queue['type'] == 'queue' :
					event['type'] = 'queue ' + act

				# No type match
				else :
					continue

				# For interrupt, which is not declared explicitly
				if task_id not in tasks :
					task = {}
					task['no'] = str(len(tasks) + 1)
					task['priority'] = -1
					task['name'] = task['no'] + ": Interrupt " + task_id

					tasks[task_id] = task

				events.append(event);
		
		elif act == 'block' :
			time, task_id, id = args.split(' ')
			if id in all_queues and all_queues[id]['type'] == 'binary semaphore':
				event = {}
				event['target'] = id
				event['time'] = float(time) / 1000
				event['type'] = 'semaphore block'
				event['task'] = task_id

				events.append(event);

	elif inst == 'interrupt' :
		argv = (args + ' ').split(' ')
		dir, time, int_num = argv[0:3]

		if TRACE_INTERRUPT :
			if int_num not in tasks :
				task = {}
				task['no'] = str(len(tasks) + 1)
				task['priority'] = -int(argv[3]) - 1
				task['name'] = task['no'] + ": Interrupt " + int_num
				tasks[int_num] = task

			event = {}
			event['time'] = float(time) / 1000
			event['task'] = int_num

			if dir == 'in' :
				event['type'] = 'interrupt in'
				event['prev'] = last_task
				tasks[int_num]['prev'] = last_task
				last_task = int_num

			else :
				event['type'] = 'interrupt out'
				event['prev'] = tasks[int_num]['prev']
				last_task = tasks[int_num]['prev']

			events.append(event)
			tasks[int_num]['created'] = True if dir == 'in' else False

log.close()

grasp = open('sched.grasp', 'w')

for id in tasks :
	task = tasks[id]
	grasp.write('newTask task%s -priority %s %s -name "%s"\n' % (id, task['priority'], '-kind isr' if int(id) < 256 else '', task['name']))

for id in mutexes :
	mutex = mutexes[id]
	grasp.write('newMutex mutex%s -name "%s"\n' % (id, mutex['name']))

for id in binsems :
	sem = binsems[id]
	grasp.write('newSemaphore semaphore%s -name "%s"\n' % (id, sem['name']))

for id in queues :
	queue = queues[id]
	grasp.write('newBuffer Buffer%s -name "%s"\n' % (id, queue['name']))

for id in queues :
	queue = queues[id]
	grasp.write('bufferplot 0 resize Buffer%s %s\n' % (id, queue['size']))

for id in tasks :
	task = tasks[id]
	if int(id) > 255 or not TRACE_INTERRUPT :
		grasp.write('plot 0 jobArrived job%s.1 task%s\n' % (id, id))

for event in events :
	if event['type'] == 'task out' :
		grasp.write('plot %f jobPreempted job%s.1 -target job%s.1\n' %
				    (event['time'], event['task'], event['next']))

	elif event['type'] == 'task in' :
		grasp.write('plot %f jobResumed job%s.1\n' %
					(event['time'], event['task']))

	elif event['type'] == 'mutex give' :
		grasp.write('plot %f jobReleasedMutex job%s.1 mutex%s\n' % (event['time'], event['task'], event['target']));

	elif event['type'] == 'mutex take' :
		grasp.write('plot %f jobAcquiredMutex job%s.1 mutex%s\n'% (event['time'], event['task'], event['target']));

	elif event['type'] == 'queue send' :
		grasp.write('bufferplot %f push Buffer%s "%s"\n'% (event['time'], event['target'], tasks[event['task']]['no']));

	elif event['type'] == 'queue recv' :
		grasp.write('bufferplot %f pop Buffer%s\n'% (event['time'], event['target']));

	elif event['type'] == 'semaphore give' :
		grasp.write('plot %f jobReleasedSemaphore job%s.1 semaphore%s\n' % (event['time'], event['task'], event['target']));

	elif event['type'] == 'semaphore take' :
		grasp.write('plot %f jobAcquiredSemaphore job%s.1 semaphore%s\n'% (event['time'], event['task'], event['target']));

	elif event['type'] == 'semaphore block' :
		grasp.write('plot %f jobSuspendedOnSemaphore job%s.1 semaphore%s\n'% (event['time'], event['task'], event['target']));

	elif event['type'] == 'interrupt in' :
		grasp.write('plot %f jobArrived job%s.1 task%s\n' % (event['time'], event['task'], event['task']))
		grasp.write('plot %f jobResumed job%s.1\n' % (event['time'], event['task']))
		grasp.write('plot %f jobPreempted job%s.1 -target job%s.1\n' %
				    (event['time'], event['prev'], event['task']))

	elif event['type'] == 'interrupt out' :
		grasp.write('plot %f jobCompleted job%s.1\n' % (event['time'], event['task']))
		grasp.write('plot %f jobResumed job%s.1\n' % (event['time'], event['prev']))
		
# Clean up unended operations

for id in mutexes :
	mutex = mutexes[id]
	if mutex['acquired'] :
		grasp.write('plot %f jobReleasedMutex job%s.1 mutex%s\n' %
					(events[-1]['time'], mutex['last_acquire'], id));

for id in tasks :
	task = tasks[id]
	if 'created' in task and task['created'] :
		grasp.write('plot %f jobCompleted job%s.1\n' %
					(events[-1]['time'], id))

grasp.close()
