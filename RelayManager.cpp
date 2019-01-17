/*
 * RelayManager.cpp
 *
 *  Created on: 20/04/2015
 *      Author: raulMrello
 */

#include "RelayManager.h"



//------------------------------------------------------------------------------------
//-- PRIVATE TYPEDEFS ----------------------------------------------------------------
//------------------------------------------------------------------------------------

/** Macro para imprimir trazas de depuración, siempre que se haya configurado un objeto
 *	Logger válido (ej: _debug)
 */
static const char* _MODULE_ = "[RlyMan]........";
#define _EXPR_	(_defdbg && !IS_ISR())


 
//------------------------------------------------------------------------------------
//-- PUBLIC METHODS IMPLEMENTATION ---------------------------------------------------
//------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------
RelayManager::RelayManager(PinName zc, Zerocross::LogicLevel zc_level, uint8_t num_relays, FSManager* fs, bool defdbg) : ActiveModule("RlyMan", osPriorityNormal, 3096, fs, defdbg) {
	DEBUG_TRACE_I(_EXPR_, _MODULE_, "Creando objeto");
    // Borro valor de sincronización 
    _flags = (Flags)0;
        
    // Crea lista de relés
    _max_num_relays = num_relays;
    _relay_list = new RelayHandler[_max_num_relays];
    MBED_ASSERT(_relay_list);
    for(int i = 0; i < _max_num_relays; i++){
    	_relay_list[i] = {NULL, NULL, {0,0,0}};
    }

    // Crea objeto zerocross
    _zc = new Zerocross(zc);
    MBED_ASSERT(_zc);
    _zc_level = zc_level;
    
    // borra tester zc
    _zc_test_cb = NULL;

    // Carga callbacks estáticas
    _publicationCb = callback(this, &RelayManager::publicationCb);
    DEBUG_TRACE_I(_EXPR_, _MODULE_, "Objeto listo!");
}


//------------------------------------------------------------------------------------
RelayManager::RelayManager(uint8_t num_relays, FSManager* fs, bool defdbg) : ActiveModule("RlyMan", osPriorityNormal, 3096, fs, defdbg) {
	DEBUG_TRACE_I(_EXPR_, _MODULE_, "Creando objeto");
    // Borro valor de sincronización
    _flags = (Flags)0;

    // Crea lista de relés
    _max_num_relays = num_relays;
    _relay_list = new RelayHandler[_max_num_relays];
    MBED_ASSERT(_relay_list);
    for(int i = 0; i < _max_num_relays; i++){
    	_relay_list[i] = {NULL, NULL, {0,0,0}};
    }

    // Crea objeto zerocross
    _zc = NULL;
    _zc_level = (Zerocross::LogicLevel)0;

    // borra tester zc
    _zc_test_cb = NULL;

    // Carga callbacks estáticas
    _publicationCb = callback(this, &RelayManager::publicationCb);
    DEBUG_TRACE_I(_EXPR_, _MODULE_, "Objeto listo!");
}

//------------------------------------------------------------------------------------
int32_t RelayManager::addRelayHandler(Relay* relay, RelayFeedback* fdb){
	// obtiene la referencia del relé para insertarlo en la posición correcta
	// si hay otro relé ya instalado, no lo permite
	uint8_t id = (uint8_t)relay->getId();
	if(id >= _max_num_relays){
		return -1;
	}
	if(_relay_list[id].relay != NULL){
		return -2;
	}

	// inserta en la lista
	_relay_list[id].relay = relay;
	_relay_list[id].fdb = fdb;
	return id;
}

//------------------------------------------------------------------------------------
RelayFeedback::Status RelayManager::getFeedbackResult(uint8_t id, uint32_t* t_on_us, uint32_t* t_off_us, uint32_t *t_sc_us){
	if(_relay_list[id].fdb){
		return _relay_list[id].fdb->getResult(t_on_us, t_off_us, t_sc_us, _relay_list[id].cfg.deltaUs);
	}
	// si no hay feedback, devuelve un resultado con todos los errores marcados
	return ((RelayFeedback::Status)(RelayFeedback::ErrorTimeOnHigh | RelayFeedback::ErrorTimeOnLow | RelayFeedback::ErrorTimeOffHigh | RelayFeedback::ErrorTimeOffLow));
}


//------------------------------------------------------------------------------------
osStatus RelayManager::putMessage(State::Msg *msg){
    osStatus ost = _queue.put(msg, ActiveModule::DefaultPutTimeout);
    if(ost != osOK){
        DEBUG_TRACE_E(_EXPR_, _MODULE_, "QUEUE_PUT_ERROR %d", ost);
    }
    return ost;
}




//------------------------------------------------------------------------------------
//-- PROTECTED METHODS IMPLEMENTATION ------------------------------------------------
//------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------
void RelayManager::subscriptionCb(const char* topic, void* msg, uint16_t msg_len){
    // si es un comando solicitando una acción manual...
    if(MQ::MQClient::isTokenRoot(topic, "set/value") ){
        DEBUG_TRACE_D(_EXPR_, _MODULE_, "Recibido topic %s", topic);

        // el mensaje es un blob tipo 'RlyManAction_t'
        // chequea el mensaje
        if(msg_len != sizeof(Blob::RlyManAction_t)){
        	DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_MSG, tamaño incorrecto en %s", topic);
        	return;
        }

        // crea mensaje para publicar en la máquina de estados
        State::Msg* op = (State::Msg*)Heap::memAlloc(sizeof(State::Msg));
        MBED_ASSERT(op);

        // reserva espacio, chequea y copia
        Blob::RlyManAction_t* action = (Blob::RlyManAction_t*)Heap::memAlloc(sizeof(Blob::RlyManAction_t));
        MBED_ASSERT(action);
        *action = *((Blob::RlyManAction_t*)msg);
        // aplica el tipo de mensaje en función del topic recibido
        op->sig = RelayActionPendingFlag;
        // apunta a los datos
        op->msg = action;

        // postea en la cola de la máquina de estados
        if(putMessage(op) != osOK){
        	if(op->msg){
        		Heap::memFree(op->msg);
        	}
        	Heap::memFree(op);
        }
        return;
    }

    DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_TOPIC. No se puede procesar el topic [%s]", topic);
}


//------------------------------------------------------------------------------------
State::StateResult RelayManager::Init_EventHandler(State::StateEvent* se){
	State::Msg* st_msg = (State::Msg*)se->oe->value.p;
    switch((int)se->evt){
        case State::EV_ENTRY:{
        	DEBUG_TRACE_I(_EXPR_, _MODULE_, "Iniciando recuperación de datos...");
        	// recupera los datos de memoria NV
        	restoreConfig();

        	DEBUG_TRACE_D(_EXPR_, _MODULE_, "Relay0 Ton=%d, Toff=%d, delta=%d", _relay_list[0].cfg.delayOnUs, _relay_list[0].cfg.delayOffUs, _relay_list[0].cfg.deltaUs);

        	// realiza la suscripción local ej: "cmd/$module/#"
        	char* sub_topic_local = (char*)Heap::memAlloc(MQ::MQClient::getMaxTopicLen());
        	MBED_ASSERT(sub_topic_local);
        	sprintf(sub_topic_local, "set/+/%s", _sub_topic_base);
        	if(MQ::MQClient::subscribe(sub_topic_local, new MQ::SubscribeCallback(this, &RelayManager::subscriptionCb)) == MQ::SUCCESS){
        		DEBUG_TRACE_D(_EXPR_, _MODULE_, "Sucripción LOCAL hecha a %s", sub_topic_local);
        	}
        	else{
        		DEBUG_TRACE_E(_EXPR_, _MODULE_, "ERR_SUBSC en la suscripción LOCAL a %s", sub_topic_local);
        	}
        	sprintf(sub_topic_local, "get/+/%s", _sub_topic_base);
        	if(MQ::MQClient::subscribe(sub_topic_local, new MQ::SubscribeCallback(this, &RelayManager::subscriptionCb)) == MQ::SUCCESS){
        		DEBUG_TRACE_D(_EXPR_, _MODULE_, "Sucripción LOCAL hecha a %s", sub_topic_local);
        	}
        	else{
        		DEBUG_TRACE_E(_EXPR_, _MODULE_, "ERR_SUBSC en la suscripción LOCAL a %s", sub_topic_local);
        	}
        	Heap::memFree(sub_topic_local);
            return State::HANDLED;
        }

        case State::EV_TIMED:{
            return State::HANDLED;
        }

        // Procesa datos recibidos de la publicación en $BASE/value/cmd
        case RelayActionPendingFlag:{
        	// obtiene la acción a realizar
        	_curr_action = *((Blob::RlyManAction_t*)st_msg->msg);
        	DEBUG_TRACE_D(_EXPR_, _MODULE_, "Iniciando acción sobre relé '%d'", _curr_action.id);
        	// si el relé tiene feedback asociado...
        	if(_relay_list[_curr_action.id].fdb){
				// si la operación es un ON activa el feedback
				if(_curr_action.request == Blob::RlyManOn){
					DEBUG_TRACE_D(_EXPR_, _MODULE_, "Arrancando feedback");
					_relay_list[_curr_action.id].fdb->start();
					Thread::wait(RelayFeedback::DefaultPreviousCaptureTime);
				}
				// si es un OFF lo reactiva
				else if(_curr_action.request == Blob::RlyManOff){
					DEBUG_TRACE_D(_EXPR_, _MODULE_, "Resumiendo feedback");
					_relay_list[_curr_action.id].fdb->resume();
					Thread::wait(RelayFeedback::DefaultPreviousCaptureTime);
				}
				// si es otro, habrá que notificar error
				else{
					DEBUG_TRACE_E(_EXPR_, _MODULE_, "ERR_REQ la acción es desconocida.");
					return State::HANDLED;
				}
        	}

            // activa flag de estado
            _flags = (Flags)(_flags | ActionPending);

        	// si el zerocross está habilitado
        	if(_zc){
				DEBUG_TRACE_D(_EXPR_, _MODULE_, "Iniciando Zerocross para acción sincronizada");

				// activa eventos del zerocross para ejecutar las acciones pendientes de forma sincronizada
				_zc->enableEvents(_zc_level, callback(this, &RelayManager::isrZerocrossCb));

				// queda bloqueado hasta que se complete la acción
				_sem.wait();

				// desactiva eventos del zerocross
				_zc->disableEvents(_zc_level);
        	}
        	// si no está habilitado el zc, ejecuta su callback sin esperar más
        	else{
        		isrZerocrossCb(Zerocross::EdgeActiveAreBoth);
        	}

			DEBUG_TRACE_D(_EXPR_, _MODULE_, "Fín de la acción");
			char msg;
			if(_curr_action.request == Blob::RlyManOn){
				Thread::wait(DefaultMaxCurrentTimeMs);
				if(_relay_list[_curr_action.id].fdb){
					DEBUG_TRACE_D(_EXPR_, _MODULE_, "Pausando feedback");
					_relay_list[_curr_action.id].fdb->pause();
				}
				msg = '1';
			}
			else{
				Thread::wait(DefaultMaxCurrentTimeMs/2);
				// detiene feedback y captura estado
				if(_relay_list[_curr_action.id].fdb){
					_relay_list[_curr_action.id].fdb->stop();
				}
				msg = '0';
			}


			// realiza calibración de los retardos de On y Off en función del resultado obtenido del feedback
			feedbackUpdate();

			// Notifica el cambio de estado
			char* topic = (char*)Heap::memAlloc(MQ::MQClient::getMaxTopicLen());
        	MBED_ASSERT(topic);
        	sprintf(topic, "stat/value/%s", _pub_topic_base);
        	DEBUG_TRACE_D(_EXPR_, _MODULE_, "Publicando resultado en '%s'", topic);
			MQ::MQClient::publish(topic, &_curr_action, sizeof(Blob::RlyManAction_t), &_publicationCb);

			// también habrá que notificar feedback disponible
			if(_relay_list[_curr_action.id].fdb){
				sprintf(topic, "stat/fdbk/%s", _pub_topic_base);
				DEBUG_TRACE_D(_EXPR_, _MODULE_, "Publicando resultado en '%s'", topic);
				MQ::MQClient::publish(topic, &msg, sizeof(char), &_publicationCb);
			}
			Heap::memFree(topic);
            return State::HANDLED;
        }

        case State::EV_EXIT:{
            nextState();
            return State::HANDLED;
        }

        default:{
        	return State::IGNORED;
        }

     }
}


//------------------------------------------------------------------------------------
osEvent RelayManager:: getOsEvent(){
	return _queue.get();
}


//------------------------------------------------------------------------------------
void RelayManager::publicationCb(const char* topic, int32_t result){

}


//------------------------------------------------------------------------------------
bool RelayManager::checkIntegrity(){
	for(int i=0; i<_max_num_relays; i++){
		if(_relay_list[i].cfg.delayOnUs < DefaultSwitchingDelay || _relay_list[i].cfg.delayOnUs >= MaxSwitchingDelay){
			return false;
		}
		if(_relay_list[i].cfg.delayOffUs < DefaultSwitchingDelay || _relay_list[i].cfg.delayOffUs >= MaxSwitchingDelay){
			return false;
		}
		if(_relay_list[i].cfg.deltaUs == 0){
			return false;
		}
	}
	return true;
}


//------------------------------------------------------------------------------------
void RelayManager::setDefaultConfig(){
	for(int i=0; i<_max_num_relays; i++){
		_relay_list[i].cfg.delayOnUs = DefaultSwitchingDelay;
		_relay_list[i].cfg.delayOffUs = DefaultSwitchingDelay;
		_relay_list[i].cfg.deltaUs = DefaultSwitchingDelta;
		char name[16];
		sprintf(name, "RlyManCfg_%d", i);
		saveParameter(name, &_relay_list[i].cfg, sizeof(Config_t), NVSInterface::TypeBlob);
	}
}


//------------------------------------------------------------------------------------
void RelayManager::restoreConfig(){
	DEBUG_TRACE_D(_EXPR_, _MODULE_, "Recuperando datos de memoria NV...");
	bool success = true;
	for(int i=0; i<_max_num_relays; i++){
		char name[16];
		sprintf(name, "RlyManCfg_%d", i);
		if(!restoreParameter(name, &_relay_list[i].cfg, sizeof(Config_t), NVSInterface::TypeBlob)){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_NVS leyendo UpdFlags!");
			success = false;
		}
	}

	if(success){
		DEBUG_TRACE_D(_EXPR_, _MODULE_, "Datos recuperados. Chequeando integridad...");
    	// chequea la coherencia de los datos y en caso de algo no esté bien, establece los datos por defecto
    	// almacenándolos de nuevo en memoria NV.
    	if(!checkIntegrity()){
    		DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_CFG. Ha fallado el check de integridad.");
    	}
    	else{
    		DEBUG_TRACE_D(_EXPR_, _MODULE_, "Check de integridad OK!");
    		return;
    	}
	}
	DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_FS. Error en la recuperación de datos. Establece configuración por defecto");
	setDefaultConfig();
}


//------------------------------------------------------------------------------------
void RelayManager::saveConfig(){
	DEBUG_TRACE_D(_EXPR_, _MODULE_, "Guardando datos en memoria NV...");

	// almacena en el sistema de ficheros
	for(int i=0; i<_max_num_relays; i++){
		char name[16];
		sprintf(name, "RlyManCfg_%d", i);
		if(!saveParameter(name, &_relay_list[i].cfg, sizeof(Config_t), NVSInterface::TypeBlob)){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_NVS grabando UpdFlags!");
		}
	}
}


//------------------------------------------------------------------------------------
void RelayManager::isrZerocrossCb(Zerocross::LogicLevel level){

	// si hay acciones pendientes...
	if((_flags & ActionPending) != 0){
		if(_curr_action.request == Blob::RlyManOn){
			_delay_tmr.start();
			while(_delay_tmr.read_us() < _relay_list[_curr_action.id].cfg.delayOnUs);
			_relay_list[_curr_action.id].relay->turnOn();
		}
		else if(_curr_action.request == Blob::RlyManOff){
			_delay_tmr.start();
			while(_delay_tmr.read_us() < _relay_list[_curr_action.id].cfg.delayOffUs);
			_relay_list[_curr_action.id].relay->turnOff();
		}

		// habilita tester del zero cross
		if(_zc_test_cb != (Callback<void()>)NULL){
			_zc_test_cb.call();
		}

		// borra el flag de operación pendiente
		_flags = (Flags)(_flags & ~ActionPending);

		// libera el semáforo de bloqueo
		_sem.release();

	}
}        


//------------------------------------------------------------------------------------
void RelayManager::feedbackUpdate(){

	// chequea si hay feedback habilitado
	if(_relay_list[_curr_action.id].fdb){
		// Obtiene el resultado de la última conmutación
		uint32_t ton, toff, tsc;
		RelayFeedback::Status result = _relay_list[_curr_action.id].fdb->getResult(&ton, &toff, &tsc, _relay_list[_curr_action.id].cfg.deltaUs);

		// actualizo el delta
		_relay_list[_curr_action.id].cfg.deltaUs = (uint32_t)(((100 - RelayFeedback::DefaultDeltaPercent) * tsc)/100);
		DEBUG_TRACE_D(_EXPR_, _MODULE_, "Feedback check Ton=%d, Toff=%d, Tsc=%d, delta=%d", ton, toff, tsc, _relay_list[_curr_action.id].cfg.deltaUs);

		// si hay error por exceso de tiempo de on, lo decremento
		bool updated = false;
		if((result & RelayFeedback::ErrorTimeOnHigh) != 0){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_FEEDBACK ErrorTimeOnHigh");
			_relay_list[_curr_action.id].cfg.delayOnUs -= _relay_list[_curr_action.id].cfg.deltaUs;
			updated = true;
		}
		// si es por defecto lo incremento
		if((result & RelayFeedback::ErrorTimeOnLow) != 0){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_FEEDBACK ErrorTimeOnLow");
			_relay_list[_curr_action.id].cfg.delayOnUs += _relay_list[_curr_action.id].cfg.deltaUs;
			updated = true;
		}
		if((result & RelayFeedback::ErrorTimeOffHigh) != 0){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_FEEDBACK ErrorTimeOffHigh");
			_relay_list[_curr_action.id].cfg.delayOffUs += _relay_list[_curr_action.id].cfg.deltaUs;
			updated = true;
		}
		if((result & RelayFeedback::ErrorTimeOffLow) != 0){
			DEBUG_TRACE_W(_EXPR_, _MODULE_, "ERR_FEEDBACK ErrorTimeOffLow");
			_relay_list[_curr_action.id].cfg.delayOffUs -= _relay_list[_curr_action.id].cfg.deltaUs;
			updated = true;
		}

		//si no hay errores en alguna conmutación, guardo los parámetros en memoria NV
		if(result == (RelayFeedback::Status)0 && updated){
			char name[16];
			sprintf(name, "RlyManCfg_%d", _curr_action.id);
			saveParameter(name, &_relay_list[_curr_action.id].cfg, sizeof(Config_t), NVSInterface::TypeBlob);
		}

	}
}



