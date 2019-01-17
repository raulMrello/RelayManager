/*
 * RelayManager.h
 *
 *  Created on: Sep 2017
 *      Author: raulMrello
 *
 *	RelayManager es el m�dulo encargado de gestionar las actuaciones sobre un grupo de rel�s (class Relay). Para dicha funci�n
 *	hace uso de un objeto zerocross (class Zerocross) instalando una callback en los eventos ISR del paso por cero. Adem�s para
 *	asegurar que la conmutaci�n siempre se sit�a cercana al paso por cero real, utiliza un objeto de adaptaci�n (class RelayFeedback)
 *	capaz de medir los retardos desde que se produce la ISR del zerocross hasta obtener la conmutaci�n f�sica del rel� en su
 *	contacto de salida, tanto para la conmutaci�n de On como para la de Off.
 *
 *	RelayManager se integra en el sistema MQLib, registrando unos topics base $BASE para realizar las publicaciones y suscripciones,
 *	as� escuchar� comandos de actuaci�n sobre los rel�s en $BASE/value/cmd siendo el mensaje asociado del tipo Blob::RlyManAction_t
 *
 *	Por otro lado, cuando se realice una conmutaci�n, publicar� su estado en el topic $BASE/value/stat con un mensaje del mismo
 *	tipo.
 *	Adem�s, una vez que se calcule el feedback de conmutaci�n, se publicar� un mensaje en el topic $BASE/fdbk/stat con el mensaje
 *	siendo un caracter: '1' para indicar feedback disponible tras conmutaci�n a On y '0' tras la conmutaci�n a Off.
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

    
    /** Crea un manejador de rel�s asociando por defecto la entrada de zerocross, los flancos que utilizar�
     *  as� como el m�ximo n�mero de rel�s soportados
     *  @param zc Entrada de zerocross
     *  @param zc_level Nivel de activaci�n de eventos del zerocross (flancos activos)
     *  @param num_relays N�mero m�ximo de rel�s
     * 	@param fs Objeto FSManager para operaciones de backup
     * 	@param defdbg Flag para habilitar depuraci�n por defecto
     */
    RelayManager(PinName zc, Zerocross::LogicLevel zc_level, uint8_t num_relays, FSManager* fs, bool defdbg = false);

    
    /** Crea un manejador de rel�s sin control de zerocross, y por lo tanto sin feedback.
     *  @param num_relays N�mero m�ximo de rel�s
     * 	@param fs Objeto FSManager para operaciones de backup
     * 	@param defdbg Flag para habilitar depuraci�n por defecto
     */
    RelayManager(uint8_t num_relays, FSManager* fs, bool defdbg = false);


    /** Destructor */
    ~RelayManager(){}


    /** A�ade un manejador de rel� a la lista
     *
     *  @param relay Objeto Relay
     *  @param fdb Objeto RelayFeedback o NULL si no hay feedback asociado
     *  @return Identificador creado (igual al 'id') o valor < 0 en caso de error
     */
    int32_t addRelayHandler(Relay* relay, RelayFeedback* fdb = NULL);


    /** Interfaz para postear un mensaje de la m�quina de estados en el Mailbox de la clase heredera
     *  @param msg Mensaje a postear
     *  @return Resultado
     */
    virtual osStatus putMessage(State::Msg *msg);


    /** Obtiene el resultado de la �ltima operaci�n del feedback integrado en el rel� 'id'
     *
     *	@param id Identificador del rel� del que se solicita la consulta
     *  @param t_on_us Recibe el tiempo de ON en microseg
     *  @param t_off_us Recibe el tiempo de OFF en microseg
     *  @param t_sc_us Recibe el tiempo del semiciclo en microseg
     *	@return Status con los flags de resultado o si no hay feedback, todos los flags de error activados
     */
    RelayFeedback::Status getFeedbackResult(uint8_t id, uint32_t* t_on_us, uint32_t* t_off_us, uint32_t *t_sc_us);


    /** Rutina para instalar un tester del flanco exacto del zerocross en el que se incia el proceso de conmutaci�n
     *  tanto para On como para Off.
     * @param zcTestCb Callback instalada
     */
    void attachZerocrossTester(Callback<void()> zcTestCb){
    	MBED_ASSERT(zcTestCb);
    	_zc_test_cb = zcTestCb;
    }

  private:

    /** Tiempo por defecto de la duraci�n del pico de corriente antes de bajar a mantenimiento (en millis) */
    static const uint32_t DefaultMaxCurrentTimeMs = 100;

    /** M�ximo retardo permitido en las conmutaciones (50ms) */
    static const uint32_t MaxSwitchingDelay = 50000;

    /** Retardo por defecto en las conmutaciones (8ms) */
    static const uint32_t DefaultSwitchingDelay = 8000;

    /** Delta para la validaci�n de la conmutaci�n en las conmutaciones (5% Tsc = 500us) */
    static const uint32_t DefaultSwitchingDelta = 500;

    /** M�ximo n�mero de mensajes alojables en la cola asociada a la m�quina de estados */
    static const uint32_t MaxQueueMessages = 16;

    /** Flags de operaciones a realizar por la tarea */
    enum MsgEventFlags{
        RelayActionPendingFlag  = (State::EV_RESERVED_USER << 0),       /// Indica que se ha solicitado un cambio en alg�n rel�
        MaxCurrTimeoutFlag 		= (State::EV_RESERVED_USER << 1),       /// Indica que ha finalizado el tiempo de corriente de pico
        RelayChangedFlag        = (State::EV_RESERVED_USER << 2),       /// Indica que un rel� ha cambiado de estado
        SyncUpdateFlag          = (State::EV_RESERVED_USER << 3),       /// Indica que se solicita una resincronizaci�n con el nuevo retardo enviado
        RelayToLowLevel         = (State::EV_RESERVED_USER << 4),       /// Indica que alg�n rel� debe bajar a corriente de mantenimiento
    };


    /** Cola de mensajes de la m�quina de estados */
    Queue<State::Msg, MaxQueueMessages> _queue;


    /** Tipos de flags
     *
     */
    enum Flags{
        ActionPending = (1 << 0),       /// Flag para indicar acci�n en curso pendiente
    };


    /** Estructura de configuraci�n con los par�metros de la calibraci�n de retardos de conmutaci�n
     *  para el ajuste preciso al zerocross
     */
    struct Config_t {
    	uint32_t delayOnUs;				//!< Retardo en el encendido en us
		uint32_t delayOffUs;			//!< Retardo en el apagado en us
		uint32_t deltaUs;				//!< Delta de comparaci�n en us
    };


    /** Estructura de datos que facilita el manejo de los eventos y estados relativos a cada rel�
     *
     */
    struct RelayHandler{
        Relay* relay;               /// Rel� asociado
        RelayFeedback* fdb;			/// Feedback asociado
        Config_t cfg;				/// Par�metros de configuraci�n del rel�
    };

    /** Variables de flags de estado */
    Flags _flags;

    /** N�mero de rel�s controlables por este componente */
    uint8_t		_max_num_relays;

    /** Lista de rel�s */
    RelayHandler *_relay_list;

    /** Manejador del zerocross, flanco activo */
    Zerocross *_zc;
    Zerocross::LogicLevel _zc_level;
    
    /** Callback para testear los flancos de zerocross en los que se inician las conmutaciones */
    Callback<void()> _zc_test_cb;

    /** Sem�foro para sincronizar acciones pendientes */
    Semaphore _sem{0, 1};

    /** Acci�n en curso */
    Blob::RlyManAction_t _curr_action;

    /** Timer asociado a los retardos en la conmutaci�n para ajuste al zerocross */
    Timer _delay_tmr;


    /** Interfaz para obtener un evento osEvent de la clase heredera
     *  @param msg Mensaje a postear
     */
    virtual osEvent getOsEvent();


 	/** Interfaz para manejar los eventos en la m�quina de estados por defecto
      *  @param se Evento a manejar
      *  @return State::StateResult Resultado del manejo del evento
      */
    virtual State::StateResult Init_EventHandler(State::StateEvent* se);


 	/** Callback invocada al recibir una actualizaci�n de un topic local al que est� suscrito
      *  @param topic Identificador del topic
      *  @param msg Mensaje recibido
      *  @param msg_len Tama�o del mensaje
      */
    virtual void subscriptionCb(const char* topic, void* msg, uint16_t msg_len);


 	/** Callback invocada al finalizar una publicaci�n local
      *  @param topic Identificador del topic
      *  @param result Resultado de la publicaci�n
      */
    virtual void publicationCb(const char* topic, int32_t result);


   	/** Chequea la integridad de los datos de configuraci�n <_cfg>. En caso de que algo no sea
   	 * 	coherente, restaura a los valores por defecto y graba en memoria NV.
   	 * 	@return True si la integridad es correcta, False si es incorrecta
	 */
	virtual bool checkIntegrity();


   	/** Establece la configuraci�n por defecto grab�ndola en memoria NV
	 */
	virtual void setDefaultConfig();


   	/** Recupera la configuraci�n de memoria NV
	 */
	virtual void restoreConfig();


   	/** Graba la configuraci�n en memoria NV
	 */
	virtual void saveConfig();


	/** Graba un par�metro en la memoria NV
	 * 	@param param_id Identificador del par�metro
	 * 	@param data Datos asociados
	 * 	@param size Tama�o de los datos
	 * 	@param type Tipo de los datos
	 * 	@return True: �xito, False: no se pudo recuperar
	 */
	virtual bool saveParameter(const char* param_id, void* data, size_t size, NVSInterface::KeyValueType type){
		return ActiveModule::saveParameter(param_id, data, size, type);
	}


	/** Recupera un par�metro de la memoria NV
	 * 	@param param_id Identificador del par�metro
	 * 	@param data Receptor de los datos asociados
	 * 	@param size Tama�o de los datos a recibir
	 * 	@param type Tipo de los datos
	 * 	@return True: �xito, False: no se pudo recuperar
	 */
	virtual bool restoreParameter(const char* param_id, void* data, size_t size, NVSInterface::KeyValueType type){
		return ActiveModule::restoreParameter(param_id, data, size, type);
	}
    

	/** Callback invocada al recibir un evento de zerocross. Se ejecuta en contexto ISR. Se deber� actuar sobre los
     *  rel�s lo m�s r�pidamente posible, para estar sincronizado con el zc.
     *
     *  @param level Identificador del flanco activo en el zerocross que gener� la interrupci�n
     */
    void isrZerocrossCb(Zerocross::LogicLevel level);        
    

    /** Realiza calibraci�n de los retados de On y Off en funci�n de los datos obtenidos del feedback en la �ltima
     *  conmutaci�n
     */
    void feedbackUpdate();

};
     
#endif /*__RelayManager__H */

/**** END OF FILE ****/


