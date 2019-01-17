/*
 * RelayManager.h
 *
 *  Created on: Sep 2017
 *      Author: raulMrello
 *
 *	RelayManager es el módulo encargado de gestionar las actuaciones sobre un grupo de relés (class Relay). Para dicha función
 *	hace uso de un objeto zerocross (class Zerocross) instalando una callback en los eventos ISR del paso por cero. Además para
 *	asegurar que la conmutación siempre se sitúa cercana al paso por cero real, utiliza un objeto de adaptación (class RelayFeedback)
 *	capaz de medir los retardos desde que se produce la ISR del zerocross hasta obtener la conmutación física del relé en su
 *	contacto de salida, tanto para la conmutación de On como para la de Off.
 *
 *	RelayManager se integra en el sistema MQLib, registrando unos topics base $BASE para realizar las publicaciones y suscripciones,
 *	así escuchará comandos de actuación sobre los relés en $BASE/value/cmd siendo el mensaje asociado del tipo Blob::RlyManAction_t
 *
 *	Por otro lado, cuando se realice una conmutación, publicará su estado en el topic $BASE/value/stat con un mensaje del mismo
 *	tipo.
 *	Además, una vez que se calcule el feedback de conmutación, se publicará un mensaje en el topic $BASE/fdbk/stat con el mensaje
 *	siendo un caracter: '1' para indicar feedback disponible tras conmutación a On y '0' tras la conmutación a Off.
 *
 */
 
#ifndef __RelayManager__H
#define __RelayManager__H

#include "mbed.h"
#include "ActiveModule.h"
#include "Relay.h"
#include "Zerocross.h"
#include "RelayFeedback.h"
#include "RelayManagerBlob.h"

   
class RelayManager : public ActiveModule {
  public:

    
    /** Crea un manejador de relés asociando por defecto la entrada de zerocross, los flancos que utilizará
     *  así como el máximo número de relés soportados
     *  @param zc Entrada de zerocross
     *  @param zc_level Nivel de activación de eventos del zerocross (flancos activos)
     *  @param num_relays Número máximo de relés
     * 	@param fs Objeto FSManager para operaciones de backup
     * 	@param defdbg Flag para habilitar depuración por defecto
     */
    RelayManager(PinName zc, Zerocross::LogicLevel zc_level, uint8_t num_relays, FSManager* fs, bool defdbg = false);

    
    /** Crea un manejador de relés sin control de zerocross, y por lo tanto sin feedback.
     *  @param num_relays Número máximo de relés
     * 	@param fs Objeto FSManager para operaciones de backup
     * 	@param defdbg Flag para habilitar depuración por defecto
     */
    RelayManager(uint8_t num_relays, FSManager* fs, bool defdbg = false);


    /** Destructor */
    ~RelayManager(){}


    /** Añade un manejador de relé a la lista
     *
     *  @param relay Objeto Relay
     *  @param fdb Objeto RelayFeedback o NULL si no hay feedback asociado
     *  @return Identificador creado (igual al 'id') o valor < 0 en caso de error
     */
    int32_t addRelayHandler(Relay* relay, RelayFeedback* fdb = NULL);


    /** Interfaz para postear un mensaje de la máquina de estados en el Mailbox de la clase heredera
     *  @param msg Mensaje a postear
     *  @return Resultado
     */
    virtual osStatus putMessage(State::Msg *msg);


    /** Obtiene el resultado de la última operación del feedback integrado en el relé 'id'
     *
     *	@param id Identificador del relé del que se solicita la consulta
     *  @param t_on_us Recibe el tiempo de ON en microseg
     *  @param t_off_us Recibe el tiempo de OFF en microseg
     *  @param t_sc_us Recibe el tiempo del semiciclo en microseg
     *	@return Status con los flags de resultado o si no hay feedback, todos los flags de error activados
     */
    RelayFeedback::Status getFeedbackResult(uint8_t id, uint32_t* t_on_us, uint32_t* t_off_us, uint32_t *t_sc_us);


    /** Rutina para instalar un tester del flanco exacto del zerocross en el que se incia el proceso de conmutación
     *  tanto para On como para Off.
     * @param zcTestCb Callback instalada
     */
    void attachZerocrossTester(Callback<void()> zcTestCb){
    	MBED_ASSERT(zcTestCb);
    	_zc_test_cb = zcTestCb;
    }

  private:

    /** Tiempo por defecto de la duración del pico de corriente antes de bajar a mantenimiento (en millis) */
    static const uint32_t DefaultMaxCurrentTimeMs = 100;

    /** Máximo retardo permitido en las conmutaciones (50ms) */
    static const uint32_t MaxSwitchingDelay = 50000;

    /** Retardo por defecto en las conmutaciones (8ms) */
    static const uint32_t DefaultSwitchingDelay = 8000;

    /** Delta para la validación de la conmutación en las conmutaciones (5% Tsc = 500us) */
    static const uint32_t DefaultSwitchingDelta = 500;

    /** Máximo número de mensajes alojables en la cola asociada a la máquina de estados */
    static const uint32_t MaxQueueMessages = 16;

    /** Flags de operaciones a realizar por la tarea */
    enum MsgEventFlags{
        RelayActionPendingFlag  = (State::EV_RESERVED_USER << 0),       /// Indica que se ha solicitado un cambio en algún relé
        MaxCurrTimeoutFlag 		= (State::EV_RESERVED_USER << 1),       /// Indica que ha finalizado el tiempo de corriente de pico
        RelayChangedFlag        = (State::EV_RESERVED_USER << 2),       /// Indica que un relé ha cambiado de estado
        SyncUpdateFlag          = (State::EV_RESERVED_USER << 3),       /// Indica que se solicita una resincronización con el nuevo retardo enviado
        RelayToLowLevel         = (State::EV_RESERVED_USER << 4),       /// Indica que algún relé debe bajar a corriente de mantenimiento
    };


    /** Cola de mensajes de la máquina de estados */
    Queue<State::Msg, MaxQueueMessages> _queue;


    /** Tipos de flags
     *
     */
    enum Flags{
        ActionPending = (1 << 0),       /// Flag para indicar acción en curso pendiente
    };


    /** Estructura de configuración con los parámetros de la calibración de retardos de conmutación
     *  para el ajuste preciso al zerocross
     */
    struct Config_t {
    	uint32_t delayOnUs;				//!< Retardo en el encendido en us
		uint32_t delayOffUs;			//!< Retardo en el apagado en us
		uint32_t deltaUs;				//!< Delta de comparación en us
    };


    /** Estructura de datos que facilita el manejo de los eventos y estados relativos a cada relé
     *
     */
    struct RelayHandler{
        Relay* relay;               /// Relé asociado
        RelayFeedback* fdb;			/// Feedback asociado
        Config_t cfg;				/// Parámetros de configuración del relé
    };

    /** Variables de flags de estado */
    Flags _flags;

    /** Número de relés controlables por este componente */
    uint8_t		_max_num_relays;

    /** Lista de relés */
    RelayHandler *_relay_list;

    /** Manejador del zerocross, flanco activo */
    Zerocross *_zc;
    Zerocross::LogicLevel _zc_level;
    
    /** Callback para testear los flancos de zerocross en los que se inician las conmutaciones */
    Callback<void()> _zc_test_cb;

    /** Semáforo para sincronizar acciones pendientes */
    Semaphore _sem{0, 1};

    /** Acción en curso */
    Blob::RlyManAction_t _curr_action;

    /** Timer asociado a los retardos en la conmutación para ajuste al zerocross */
    Timer _delay_tmr;


    /** Interfaz para obtener un evento osEvent de la clase heredera
     *  @param msg Mensaje a postear
     */
    virtual osEvent getOsEvent();


 	/** Interfaz para manejar los eventos en la máquina de estados por defecto
      *  @param se Evento a manejar
      *  @return State::StateResult Resultado del manejo del evento
      */
    virtual State::StateResult Init_EventHandler(State::StateEvent* se);


 	/** Callback invocada al recibir una actualización de un topic local al que está suscrito
      *  @param topic Identificador del topic
      *  @param msg Mensaje recibido
      *  @param msg_len Tamaño del mensaje
      */
    virtual void subscriptionCb(const char* topic, void* msg, uint16_t msg_len);


 	/** Callback invocada al finalizar una publicación local
      *  @param topic Identificador del topic
      *  @param result Resultado de la publicación
      */
    virtual void publicationCb(const char* topic, int32_t result);


   	/** Chequea la integridad de los datos de configuración <_cfg>. En caso de que algo no sea
   	 * 	coherente, restaura a los valores por defecto y graba en memoria NV.
   	 * 	@return True si la integridad es correcta, False si es incorrecta
	 */
	virtual bool checkIntegrity();


   	/** Establece la configuración por defecto grabándola en memoria NV
	 */
	virtual void setDefaultConfig();


   	/** Recupera la configuración de memoria NV
	 */
	virtual void restoreConfig();


   	/** Graba la configuración en memoria NV
	 */
	virtual void saveConfig();


	/** Graba un parámetro en la memoria NV
	 * 	@param param_id Identificador del parámetro
	 * 	@param data Datos asociados
	 * 	@param size Tamaño de los datos
	 * 	@param type Tipo de los datos
	 * 	@return True: éxito, False: no se pudo recuperar
	 */
	virtual bool saveParameter(const char* param_id, void* data, size_t size, NVSInterface::KeyValueType type){
		return ActiveModule::saveParameter(param_id, data, size, type);
	}


	/** Recupera un parámetro de la memoria NV
	 * 	@param param_id Identificador del parámetro
	 * 	@param data Receptor de los datos asociados
	 * 	@param size Tamaño de los datos a recibir
	 * 	@param type Tipo de los datos
	 * 	@return True: éxito, False: no se pudo recuperar
	 */
	virtual bool restoreParameter(const char* param_id, void* data, size_t size, NVSInterface::KeyValueType type){
		return ActiveModule::restoreParameter(param_id, data, size, type);
	}
    

	/** Callback invocada al recibir un evento de zerocross. Se ejecuta en contexto ISR. Se deberá actuar sobre los
     *  relés lo más rápidamente posible, para estar sincronizado con el zc.
     *
     *  @param level Identificador del flanco activo en el zerocross que generó la interrupción
     */
    void isrZerocrossCb(Zerocross::LogicLevel level);        
    

    /** Realiza calibración de los retados de On y Off en función de los datos obtenidos del feedback en la última
     *  conmutación
     */
    void feedbackUpdate();

};
     
#endif /*__RelayManager__H */

/**** END OF FILE ****/


