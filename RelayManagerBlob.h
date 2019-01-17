/*
 * RelayManagerBlob.h
 *
 *  Created on: Ene 2018
 *      Author: raulMrello
 *
 *	RelayManagerBlob es el componente del m�dulo RelayManager en el que se definen los objetos y tipos relativos a
 *	los objetos BLOB de este m�dulo.
 *	Todos los tipos definidos en este componente est�n asociados al namespace "Blob", de forma que puedan ser
 *	accesibles mediante el uso de: "Blob::"  e importando este archivo de cabecera.
 */
 
#ifndef __RelayManagerBlob__H
#define __RelayManagerBlob__H

#include "Blob.h"
#include "mbed.h"
  

namespace Blob {


 /** Flags de evento que utiliza RelayManager para notificar un cambio de estado
  */
 enum RlyManEvtFlags{
	 RlyManOff  	= (1 << 0),		//!< Evento al cambiar un rel� a Off
	 RlyManOn 		= (1 << 1),		//!< Evento al cambiar un rel� a On
 };


 /** Estructura de datos para la solicitud de acciones
  * 	Se forma por:
  * 	@var id Identificador del rel� sobre el que actuar
  * 	@var request Acci�n a realizar
  */
struct __packed RlyManAction_t{
 	uint8_t id;
 	RlyManEvtFlags request;
 };




}



#endif
