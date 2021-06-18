#include <iostream>
#include <thread>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <atomic>
#include <vector>
#include <utility>
#include <time.h>
#include <condition_variable>
#include <string>
#include <ncurses.h>

enum Fuel { // typy paliwa
    petrol,
    diesel,
    electric
};

std::string toString(Fuel fuel) // do wyświetlania info dystrybutorów
{
	std::string s;
	if (fuel == Fuel::petrol) s = "petrol";
	else if (fuel == Fuel::diesel) s = "diesel";
	else s = "electric";
	return s;
}

enum ClientStatus { // status (miejsce w "cyklu życia") samochodu-klienta
	wStation,	// czeka na wjazd na stację
	wQueue,		// w kolejce do dystrybutora
    wRefuel,	// czeka na wolny dystrybutor
    wQPark,		// czeka na odjechanie od dystrybutora
    qPark,		// w drodze na parking
    wPark,		// czeka na wolne miejsce parkingowe
    wQStoreIn,	// czeka żeby iść do sklepu
    qStoreIn,	// idzie do sklepu
    wDoorIn,	// czeka żeby wejść
    qCashDesk,	// w kolejce do kasy
    wCashDesk,	// czeka na wolną kasę
    wQCashOut,	// czeka żeby odejść od kasy
    qCashOut,	// odchodzi od kasy
    wDoorOut,	// czeka żeby wyjść
    qStoreOut,	// wraca na parking
    wOut,		// czeka żeby wyjechać z parkingu
    qOut,		// czeka żeby wyjechać ze stacji
    Driving		// jeździ (zanim wróci znowu zatankować)
};

enum CashierStatus {
	onBreak,		// robi sobie przerwę
	readyToWork,	// gotowy do pracy, czeka na kasę do obsługiwania
	working,		// obsługuje klientów
};

std::condition_variable cvP; // cysterna diesel
std::condition_variable cvD; // cysterna benzyna
std::mutex tankMtx;	// mutex dla cystern

class Door {
	public:
		Door()
		{
			this->available = true;
		}
		bool goIfAvailable() // przejdź przez drzwi
		{
			std::lock_guard<std::mutex> lock(mtx);
			if(this->available)
			{
				this->available = false;
				return true;
			}
			else return false;
		}

		bool getAvailable(){
			return this->available;
		}
		
		void setAvailable(bool set)
		{
			std::lock_guard<std::mutex> lock(mtx);
			this->available = set;
		}
	private:
		std::mutex mtx;
		bool available;
};

class Distributor { // dystrybutor
	public:
		Distributor(int id, Fuel fuel, int capacity, int x, int y){
			this->id = id;
			this->x = x;
			this->y = y;
			this->type = fuel;
			this->capacity = capacity;
			this->fuelAmount = capacity;
			this->info = "Distributor "+std::to_string(this->id)+": "+toString(this->type)+
			" "+std::to_string(this->fuelAmount)+"/"+std::to_string(this->capacity);
			this->model = "|"+std::to_string(id)+":"+toString(this->type)+"| ";
		}
		
		void setFuelAmount(int amount){
			this->fuelAmount = amount;
		}
		
		int getFuelAmount(){
			return this->fuelAmount;
		}
		
		Fuel getFuelType(){
			return this->type;
		}
		
		std::string getInfo(){
			return this->info;
		}

		std::string getModel(){
			if (this->type == Fuel::electric) return this->model;
			else return this->model+std::to_string(this->fuelAmount)+"/"+std::to_string(this->capacity);
		}

		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}
		
		bool useIfAvailable(int amount){ // użycie przez samochód (Client)
			std::lock_guard<std::mutex> lock(mtx);
			if(this->available && this->fuelAmount >= amount)
			{
				this->available = false;
				if (this->type != Fuel::electric) this->fuelAmount -= amount;
				this->info = "Distributor "+std::to_string(this->id)+": "+toString(this->type)+
				" "+std::to_string(this->fuelAmount)+"/"+std::to_string(this->capacity);
				return true;
			} else {
				if (this->fuelAmount < amount)
				{
					this->needsRefill = true;
					if (this->type == Fuel::petrol) cvP.notify_all();
					else cvD.notify_all();
				}
				return false;
			}
		}
		
		bool fillIfAvailable(){ // napełnienie przez cysternę (Tank)
			std::lock_guard<std::mutex> lock(mtx);
			if(this->available && this->needsRefill)
			{
				this->available = false;
				this->fuelAmount = this->capacity;
				this->info = "Distributor "+std::to_string(this->id)+": "+toString(this->type)+
				" "+std::to_string(this->fuelAmount)+"/"+std::to_string(this->capacity);
				this->needsRefill = false;
				return true;
			} else {
				return false;
			}
		}
		
		void setAvailable(bool av){
			std::lock_guard<std::mutex> lock(mtx);
			this->available = av;
		}
		
	private:
		int id;
		int x;
		int y;
		Fuel type;
		int capacity;
		int fuelAmount;
		std::string info = "";
		std::string model = "";
		bool available = true;
		bool needsRefill = false;
		std::mutex mtx;
};

class CarSlot { // miejsce - na parkingu lub w jakiejś kolejce/drodze
	public:
		CarSlot(int id, int x, int y){
			this->id = id;
			this->x = x;
			this->y = y;
			this->takenBy = -1;
			this->info = "Parking space "+std::to_string(this->id)+" is free";
		}
		
		int getId(){
			return this->id;
		}
		
		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}
		
		bool takeIfAvailable() // zajmij miejsce
		{
			std::lock_guard<std::mutex> lock(mtx);
			if(this->available)
			{
				this->available = false;
				return true;
			}
			else return false;
		}
		
		void setAvailable(bool set)
		{
			std::lock_guard<std::mutex> lock(mtx);
			this->available = set;
			if(set) {
				this->takenBy = -1;
				this->info = "Parking space "+std::to_string(this->id)+" is free";
			}
		}
		
		void setTakenBy(int id)
		{
			std::lock_guard<std::mutex> lock(mtx);
			this->takenBy = id;
			this->info = "Parking space "+std::to_string(this->id)+" is taken by Car "+std::to_string(id);
		}
		
		std::string getInfo()
		{
			return this->info;
		}

		std::string getTakenBy(){
			std::string t = "";
			if (this->takenBy == -1) t = "   ";
			else if (this->takenBy < 10) t = "( "+std::to_string(this->takenBy)+")";
			else t = "("+std::to_string(this->takenBy)+")";
			return t;
		}
		
	private:
		int id;
		int x;
		int y;
		int takenBy;
		std::string info ="";
		bool available = true;
		std::mutex mtx;
};


class CashDesk { // kasa
	public:
		CashDesk(int id, int x, int y)
		{
			this->id = id;
			this->x = x;
			this->y = y;
			this->moneyOnCounter = 0;
			this->inUse = false;
		}
		
		bool payIfAvailable(int money) // dla klienta
		{
			std::lock_guard<std::mutex> lock(clientMtx);
			if(this->available && this->inUse)
			{
				this->available = false;
				this->moneyOnCounter = money;
				this->cvC.notify_all();
				return true;
			}
			else return false;
		}
		
		void setAvailable(bool set)
		{
			std::lock_guard<std::mutex> lock(clientMtx);
			this->available = set;
			if(set) this->moneyOnCounter = 0;
		}
		
		bool useIfAvailable() // dla kasjera
		{
			std::lock_guard<std::mutex> lock(cashierMtx);
			if (!this->inUse)
			{
				this->inUse = true;
				return true;
			}
			else return false;
		}
		
		int takeMoney()
		{
			std::lock_guard<std::mutex> lock(cashierMtx);
			int taken = this->moneyOnCounter;
			this->moneyOnCounter = 0;
			return taken;
		}
		
		void setInUse(bool set)
		{
			std::lock_guard<std::mutex> lock(cashierMtx);
			this->inUse = set;
		}

		bool getInUse(){
			return this->inUse;
		}
		
		int getId(){
			return this->id;
		}

		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}

		std::string getModel(){
			std::string m = "$"+std::to_string(this->id);
			return m;
		}
		
		std::mutex& getWaitingMtx()
		{
			return this->waitMtx;
		}
		std::condition_variable& getCv()
		{
			return this->cvC;
		}
		
	private:
		int id;
		int x;
		int y;
		bool available = true;
		bool inUse = false;
		int moneyOnCounter;
		std::mutex clientMtx;
		std::mutex cashierMtx;
		std::mutex waitMtx;
		std::condition_variable cvC;
};

class StationEquipment {
	public:
		static std::vector<Distributor*>& getDistributors(){
			return dist;}
		
		static std::vector<CarSlot*>& getPetrolQueue(){
			return distQueueP;}
		
		static std::vector<CarSlot*>& getDieselQueue(){
			return distQueueD;}
		
		static std::vector<CarSlot*>& getElectricQueue(){
			return distQueueE;}
		
		static std::vector<CarSlot*>& getEmergencyQueue(){
			return distQueueI;}
		
		static std::vector<CarSlot*>& getParking(){
			return parking;}
		
		static std::vector<CarSlot*>& getEmergencyParking(){
			return emergencyParking;}
		
		static std::vector<CarSlot*>& getParkQueue(){
			return parkQueue;}
		
		static std::vector<CarSlot*>& getEmergencyParkQueue(){
			return parkQueueI;}
		///////
		static std::vector<CarSlot*>& getStoreQueueIN(){
			return storeQueueIN;}
			
		static std::vector<CarSlot*>& getStoreQueueOUT(){
			return storeQueueOUT;}
			
		static std::vector<CarSlot*>& getStoreEmergencyQueueIN(){
			return storeEmergencyQueueIN;}
			
		static std::vector<CarSlot*>& getStoreEmergencyQueueOUT(){
			return storeEmergencyQueueOUT;}
			
		static std::vector<CarSlot*>& getCashQueueIN(){
			return cashQueueIN;}
			
		static std::vector<CarSlot*>& getCashQueueOUT(){
			return cashQueueOUT;}
			
		static std::vector<CarSlot*>& getCashEmergencyQueueIN(){
			return cashEmergencyQueueIN;}
			
		static std::vector<CarSlot*>& getCashEmergencyQueueOUT(){
			return cashEmergencyQueueOUT;}
			
		static std::vector<CarSlot*>& getOutQueue(){
			return outQueue;}
			
		static std::vector<CarSlot*>& getEmergencyOutQueue(){
			return outQueueI;}
		//////
		static Door& getStationDoor(){
			return stationDoor;}
			
		static std::vector<CashDesk*>& getCashDesks(){
		return cashDesks;}
		
		static int getQSize(){
			return distQSize;}
			
		static void setQSize(int size){
			distQSize = size;}
			
		static void setEmergencyRefuel(bool set){
			emergencyRefuel = set;}
			
		static bool getEmergencyRefuel(){
			return emergencyRefuel;}
			
		static void setEmergencyDoor(bool set){
			emergencyDoor = set;}
			
		static bool getEmergencyDoor(){
			return emergencyDoor;}
			
		static void setEmergencyPay(bool set){
			emergencyPay = set;}
			
		static bool getEmergencyPay(){
			return emergencyPay;}
		
		static bool breakIfPossible(bool brk)
		{
			std::lock_guard<std::mutex> lock(breakMtx);
			if (brk)
			{
				if (cashiersWorking > 1)
				{
					cashiersWorking--;
					return true;
				}
				else return false;
			} else
			{
				cashiersWorking++;
				return true;
			}
		}
		static int getCashiersWorking()
		{
			return cashiersWorking;
		}
		
	private:
		static int distQSize;
		
		static bool emergencyRefuel;
		static bool emergencyDoor;
		static bool emergencyPay;
		
		static std::vector<Distributor*> dist;
		
		static std::vector<CarSlot*> distQueueP;
		static std::vector<CarSlot*> distQueueD;
		static std::vector<CarSlot*> distQueueE;
		static std::vector<CarSlot*> distQueueI;
		
		static std::vector<CarSlot*> parkQueue;
		static std::vector<CarSlot*> parkQueueI;
		
		static std::vector<CarSlot*> parking;
		static std::vector<CarSlot*> emergencyParking;
		
		static std::vector<CarSlot*> storeQueueIN;
		static std::vector<CarSlot*> storeQueueOUT;
		static std::vector<CarSlot*> storeEmergencyQueueIN;
		static std::vector<CarSlot*> storeEmergencyQueueOUT;
		
		static Door stationDoor;
		
		static std::vector<CarSlot*> cashQueueIN;
		static std::vector<CarSlot*> cashQueueOUT;
		static std::vector<CarSlot*> cashEmergencyQueueIN;
		static std::vector<CarSlot*> cashEmergencyQueueOUT;
		
		static std::vector<CarSlot*> outQueue;
		static std::vector<CarSlot*> outQueueI;
		
		static std::vector<CashDesk*> cashDesks;
		static int cashiersWorking;
		static std::mutex breakMtx;
	
};
int StationEquipment::distQSize;
bool StationEquipment::emergencyRefuel = false;
bool StationEquipment::emergencyDoor = false;
bool StationEquipment::emergencyPay = false;
std::vector<Distributor*> StationEquipment::dist;
std::vector<CarSlot*> StationEquipment::distQueueP;
std::vector<CarSlot*> StationEquipment::distQueueD;
std::vector<CarSlot*> StationEquipment::distQueueE;
std::vector<CarSlot*> StationEquipment::distQueueI;
std::vector<CarSlot*> StationEquipment::parking;
std::vector<CarSlot*> StationEquipment::emergencyParking;
std::vector<CarSlot*> StationEquipment::parkQueue;
std::vector<CarSlot*> StationEquipment::parkQueueI;

std::vector<CarSlot*> StationEquipment::storeQueueIN;
std::vector<CarSlot*> StationEquipment::storeQueueOUT;
std::vector<CarSlot*> StationEquipment::storeEmergencyQueueIN;
std::vector<CarSlot*> StationEquipment::storeEmergencyQueueOUT;

std::vector<CarSlot*> StationEquipment::cashQueueIN;
std::vector<CarSlot*> StationEquipment::cashQueueOUT;
std::vector<CarSlot*> StationEquipment::cashEmergencyQueueIN;
std::vector<CarSlot*> StationEquipment::cashEmergencyQueueOUT;

std::vector<CarSlot*> StationEquipment::outQueue;
std::vector<CarSlot*> StationEquipment::outQueueI;

Door StationEquipment::stationDoor;
std::vector<CashDesk*> StationEquipment::cashDesks;
std::mutex StationEquipment::breakMtx;
int StationEquipment::cashiersWorking = 0;

class Cashier { // kasjer
	public:
		Cashier(int id)
		{
			this->id = id;
			this->status = CashierStatus::onBreak;
			this->x = 98;
			this->y = 33+(this->id);
			this->moneyMade = 0;
			this->clientsServed = 0;
			//this->info = std::to_string(this->moneyMade)+" Cashier "+std::to_string(this->id)+" is is waiting for a cash desk to work at";
		}
		
		std::string getInfo(){
			return this->info;
		}

		std::string getModel(){
			
			return "@"+std::to_string(this->id);
		}

		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}
		
		void start(){
			this->isWorking = true;
			lifeThread = std::make_unique<std::thread>([this]() {act(); });
		}
		
		void stop(){
			this->isWorking = false;
			this->lifeThread->join();
			this->info = std::to_string(this->moneyMade)+" Cashier "+std::to_string(this->id)+" is dead";
		}
		
		void sleepForTime(int timex100){
			for (int i=0;i<timex100;i++)
			{
				this->progress = (100*i)/timex100;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			this->progress = 100;
		}

		int getProgress(){
			return this->progress;
		}

		int getMoneyMade(){
			return this->moneyMade;
		}
		
		void act(){
			while (this->isWorking.load())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if(this->status == CashierStatus::readyToWork)
				{
					for (int i=0;i<StationEquipment::getCashDesks().size();i++)
					{
						if (StationEquipment::getCashDesks().at(i)->useIfAvailable())
						{
							this->status = CashierStatus::working;
							this->cashDeskId = i;
							this->x = StationEquipment::getCashDesks().at(this->cashDeskId)->getX() - 3;
							this->y = StationEquipment::getCashDesks().at(this->cashDeskId)->getY();
							break;
						}
						this->info = std::to_string(this->moneyMade)+" Cashier "+std::to_string(this->id)+" is waiting for a cash desk to work at";
					}
				}
				else if (this->status == CashierStatus::working)
				{
					this->info = std::to_string(this->moneyMade)+" Cashier "+std::to_string(this->id)+" is working at cash desk "+std::to_string(this->cashDeskId);
					std::unique_lock<std::mutex> lck(StationEquipment::getCashDesks().at(this->cashDeskId)->getWaitingMtx());
					StationEquipment::getCashDesks().at(this->cashDeskId)->getCv().wait(lck); // czeka na klienta
					sleepForTime(25);
					int moneyTaken = StationEquipment::getCashDesks().at(this->cashDeskId)->takeMoney();
					this->moneyMade += moneyTaken;
					this->progress = 0;
					
					this->clientsServed++;
					if (this->clientsServed >= 4)
					{
						if (StationEquipment::breakIfPossible(true))
						{
							StationEquipment::getCashDesks().at(this->cashDeskId)->setInUse(false);
							this->status = CashierStatus::onBreak;
							this->clientsServed = 0;
						}
					}
				}
				else if (this->status == CashierStatus::onBreak)
				{
					//sleepForTime(35);
					this->info = std::to_string(this->moneyMade)+" Cashier "+std::to_string(this->id)+" is taking a break";
					this->x = 98;
					this->y = 33+(this->id);
					sleepForTime(80);
					this->status = CashierStatus::readyToWork;
					StationEquipment::breakIfPossible(false);
					this->progress = 0;
				}
			}
		}
	private:
		int id;
		int x;
		int y;
		int cashDeskId;
		int moneyMade;
		int clientsServed;
		int progress = 0;
		std::atomic_bool isWorking;
		CashierStatus status;
		std::unique_ptr<std::thread> lifeThread;
		std::string info = "";
};

class Tank { // cysterna
	public:
		Tank(int id, Fuel fuel, int x, int y){
			this->id = id;
			this->type = fuel;
			this->info = "Tank "+std::to_string(this->id)+" is waiting for a distributor that needs refilling with "+toString(this->type);
			if(this->type == Fuel::petrol) this->model = "{{Pb}}";
			else this->model = "{{ON}}";
			this->x=x; this->baseX=x;
			this->y=y; this->baseY=y;
		}
		
		void start(){
			this->isDriving = true;
			lifeThread = std::make_unique<std::thread>([this]() {act(); });
		}

		void stop(){
			this->isDriving = false;
			this->lifeThread->join();
			this->info = "Tank "+std::to_string(this->id)+" is dead";
		}
		
		bool lookForDistributor(){
			for (int i=0;i<StationEquipment::getDistributors().size();i++)
			{
				if (StationEquipment::getDistributors().at(i)->getFuelType() == this->type)
				{
					if (StationEquipment::getDistributors().at(i)->fillIfAvailable())
					{
						usedDistributor = i;
						return true;
					}
				}
			}
			return false;
		}
		
		std::string getInfo(){
			return this->info;
		}

		std::string getModel(){
			return this->model;	
		}

		Fuel getType(){
			return this->type;
		}

		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}
		
		int getProgress(){
			return this->progress;
		}
		
		void sleepForTime(int timex100){
			for (int i=0;i<timex100;i++)
			{
				this->progress = (100*i)/timex100;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			this->progress = 100;
		}
		
		void act(){
			while (this->isDriving.load())
			{
				this->progress = 0;
				this->x = this->baseX;
				this->y = this->baseY;
				std::unique_lock<std::mutex> lck(tankMtx);
				if (this->type == Fuel::petrol) cvP.wait(lck);
				else cvD.wait(lck);
				
				if (lookForDistributor())
				{
					this->info = "Tank "+std::to_string(this->id)+" is refilling distributor nr "+
					std::to_string(this->usedDistributor)+ " with "+toString(this->type);
					this->x = StationEquipment::getDistributors().at(this->usedDistributor)->getX();
					this->y = StationEquipment::getDistributors().at(this->usedDistributor)->getY();
					int a = this->usedDistributor;
					if (a%2==0){
						this->y--;
					} else {
						this->y++;
					}
					sleepForTime(30+(rand()%15));
					StationEquipment::getDistributors().at(this->usedDistributor)->setAvailable(true);
					this->info = "Tank "+std::to_string(this->id)+" is waiting for a distributor that needs refilling with "+toString(this->type);
				} else {
					this->info = "Tank "+std::to_string(this->id)+" is waiting for a distributor that needs refilling with "+toString(this->type);
				}	
			}
		}
		
	private:
		int id;
		int baseX;
		int baseY;
		int x;
		int y;
		Fuel type;
		
		int usedDistributor;
		std::string info = "";
		std::string model = "";
		int progress = 0;
		std::atomic_bool isDriving;
		std::unique_ptr<std::thread> lifeThread;
};


class Client { //klient - samochód
	public:
		Client(int id, Fuel fuel, int toTank, bool emerg){
			this->id = id;
			this->x = 0;
			this->count = 0;
			this->y = 0;
			this->type = fuel;
			this->capacity = toTank;
			this->emergency = emerg;
			this->status = ClientStatus::Driving;
			this->info = "Car "+std::to_string(this->id)+" is started";
			if (emerg){
				if (id<10) this->model = "< "+std::to_string(this->id)+">";
				else this->model = "<"+std::to_string(this->id)+">";
			} else {
				if (id<10) this->model = "[ "+std::to_string(this->id)+"]";
				else this->model = "["+std::to_string(this->id)+"]";
			}
		}
		
		int getId(){
			return this->id;
		}

		int getX(){
			return this->x;
		}

		int getY(){
			return this->y;
		}
		
		std::string getInfo(){
			return this->info;
		}

		std::string getModel(){
			return this->model;
		}
		
		int getProgress(){
			return this->progress;
		}

		int getCount(){
			return this->count;
		}

		bool getEmerg(){
			return this->emergency;
		}

		Fuel getType(){
			return this->type;
		}
		
		void start(){
			this->isDriving = true;
			lifeThread = std::make_unique<std::thread>([this]() {act(); });
		}

		void stop(){
			this->isDriving = false;
			this->lifeThread->join();
			this->info = "Car "+std::to_string(this->id)+" is dead";
		}
		
		bool lookForDistributor(){
			if (StationEquipment::getEmergencyRefuel() && !this->emergency) return false;
			
			for (int i=0;i<StationEquipment::getDistributors().size();i++)
			{
				if (StationEquipment::getDistributors().at(i)->getFuelType() == this->type)
				{
					if (StationEquipment::getDistributors().at(i)->useIfAvailable(this->amountToTank))
					{
						usedDistributor = i;
						count++;
						return true;
					}
				}
			}
			return false;
		}
		
		bool lookForParking(){
			for (int i=0;i<StationEquipment::getParking().size();i++)
			{
				if(StationEquipment::getParking().at(i)->takeIfAvailable())
				{
					this->parkingSlot = i;
					return true;
				}
			}
			return false;
		}
		
		bool lookForEmergencyParking(){
			for (int i=0;i<StationEquipment::getEmergencyParking().size();i++)
			{
				if(StationEquipment::getEmergencyParking().at(i)->takeIfAvailable())
				{
					this->parkingSlot = i;
					return true;
				}
			}
			return false;
		}
		
		bool lookForCashDesk(){
			if (StationEquipment::getEmergencyPay() && !this->emergency) return false;
			int mult = 1;
			if (this->type == Fuel::petrol) mult = 5;
			else if (this->type == Fuel::diesel) mult = 4;
			for (int i=0;i<StationEquipment::getCashDesks().size();i++)
			{
				if (StationEquipment::getCashDesks().at(i)->payIfAvailable((this->amountToTank)*mult))
				{
					this->usedCashDesk = i;
					return true;
				}
			}
			return false;
		}
		
		void sleepForTime(int timex100){
			for (int i=0; i<timex100 ;i++)
			{
				this->progress = (100*i)/timex100;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			this->progress = 100;
		}
		
		void act(){
			while (this->isDriving.load())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (this->status == ClientStatus::wStation) //czeka na wjazd na stację
				{
					this->info = "Car "+std::to_string(this->id)+" is waiting to get to station";
					if (this->emergency)
					{
						if(StationEquipment::getEmergencyQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getEmergencyQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getEmergencyQueue().at(this->posInQ)->getY();
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue";
							this->status = ClientStatus::wQueue;
						}
					}
					else if (this->type == Fuel::petrol) 
					{
						if(StationEquipment::getPetrolQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getPetrolQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getPetrolQueue().at(this->posInQ)->getY();
							this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							this->status = ClientStatus::wQueue;
						}
					}
					else if (this->type == Fuel::diesel) 
					{
						if(StationEquipment::getDieselQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getDieselQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getDieselQueue().at(this->posInQ)->getY();
							this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							this->status = ClientStatus::wQueue;
						}
					}
					else 
					{
						if(StationEquipment::getElectricQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getElectricQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getElectricQueue().at(this->posInQ)->getY();
							this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							this->status = ClientStatus::wQueue;
						}
					}
				}
				else if (this->status == ClientStatus::wQueue) // czeka w kolejce do dystrybutorów
				{
					if (this->emergency)
					{
						if(StationEquipment::getEmergencyQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getEmergencyQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getEmergencyQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getEmergencyQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wRefuel;
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting for free "+toString(this->type)+" distributor";
							}
							else
							{ 
								this->info = " EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							}
						}
					}
					else if (this->type == Fuel::petrol)
					{
						if(StationEquipment::getPetrolQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getPetrolQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getPetrolQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getPetrolQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wRefuel;
								this->info = "Car "+std::to_string(this->id)+" is waiting for free "+toString(this->type)+" distributor";
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							}
						}	
					}
					else if (this->type == Fuel::diesel)
					{
						if(StationEquipment::getDieselQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getDieselQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getDieselQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getDieselQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wRefuel;
								this->info = "Car "+std::to_string(this->id)+" is waiting for free "+toString(this->type)+" distributor";
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							}
						}	
					}
					else if (this->type == Fuel::electric)
					{
						if(StationEquipment::getElectricQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getElectricQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getElectricQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getElectricQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wRefuel;
								this->info = "Car "+std::to_string(this->id)+" is waiting for free "+toString(this->type)+" distributor";
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in "+toString(this->type)+" queue";
							}
						}	
					}
				}
				else if (this->status == ClientStatus::wRefuel) // czeka na wolny dystrybutor 
				{
					if (this->emergency) StationEquipment::setEmergencyRefuel(true);
					if (lookForDistributor())
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						if (this->emergency) {
							StationEquipment::getEmergencyQueue().at(this->posInQ)->setAvailable(true);
							StationEquipment::setEmergencyRefuel(false);
						}
						else if (this->type == Fuel::petrol) StationEquipment::getPetrolQueue().at(this->posInQ)->setAvailable(true);
						else if (this->type == Fuel::diesel) StationEquipment::getDieselQueue().at(this->posInQ)->setAvailable(true);
						else StationEquipment::getElectricQueue().at(this->posInQ)->setAvailable(true);
						
						this->info = "Car "+std::to_string(this->id)+" is refueling "+toString(this->type)+" from distributor nr "+std::to_string(this->usedDistributor);

						this->x = StationEquipment::getDistributors().at(this->usedDistributor)->getX();
						this->y = StationEquipment::getDistributors().at(this->usedDistributor)->getY();
						int a = this->usedDistributor;
						if (a%2==0){
							this->y--;
						} else {
							this->y++;
						}

						sleepForTime((this->amountToTank)*2);
						//StationEquipment::getDistributors().at(this->usedDistributor)->setAvailable(true); przeniesione poniżej
						this->status = ClientStatus::wQPark;
					} else {
						this->info = "Car "+std::to_string(this->id)+" is waiting for free "+toString(this->type)+" distributor";
					}
				}
				else if(this->status == ClientStatus::wQPark)
				{
					if (this->emergency)
					{
						if(StationEquipment::getEmergencyParkQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								StationEquipment::getDistributors().at(this->usedDistributor)->setAvailable(true); // przeniesione z góry
								this->posInQ = StationEquipment::getQSize()-1;
								this->x = StationEquipment::getEmergencyParkQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getEmergencyParkQueue().at(this->posInQ)->getY();
								this->progress = 0;
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to parking";
								this->status = ClientStatus::qPark;
							}
						else {
						this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting to get into the emergency parking queue";
						}
					}
					else 
					{
						if(StationEquipment::getParkQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								StationEquipment::getDistributors().at(this->usedDistributor)->setAvailable(true); // przeniesione z góry
								this->posInQ = StationEquipment::getQSize()-1;
								this->x = StationEquipment::getParkQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getParkQueue().at(this->posInQ)->getY();
								this->progress = 0;
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to parking";
								this->status = ClientStatus::qPark;
							}
						else {
						this->info = "Car "+std::to_string(this->id)+" is waiting to get into the parking queue";
						}
					}
				}
				else if(this->status == ClientStatus::qPark)
				{
					if (this->emergency)
					{
						if(StationEquipment::getEmergencyParkQueue().at(this->posInQ-1)->takeIfAvailable())
						{
								StationEquipment::getEmergencyParkQueue().at(this->posInQ)->setAvailable(true);
								this->posInQ -= 1;
								this->x = StationEquipment::getEmergencyParkQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getEmergencyParkQueue().at(this->posInQ)->getY();
								
								if (this->posInQ == 0)
								{
									this->status = ClientStatus::wPark;
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting for free parking space";
								}
								else
								{ 
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency parking queue";
								}
						}	
					}
					else
					{
						if(StationEquipment::getParkQueue().at(this->posInQ-1)->takeIfAvailable())
						{
								StationEquipment::getParkQueue().at(this->posInQ)->setAvailable(true);
								this->posInQ -= 1;
								this->x = StationEquipment::getParkQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getParkQueue().at(this->posInQ)->getY();
								
								if (this->posInQ == 0)
								{
									this->status = ClientStatus::wPark;
									this->info = "Car "+std::to_string(this->id)+" is waiting for free parking space";
								}
								else
								{ 
									this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in parking queue";
								}
						}
					}
				}
				else if(this->status == ClientStatus::wPark)
				{
					if (this->emergency)
					{
						if (lookForEmergencyParking())
						{
							StationEquipment::getEmergencyParking().at(this->parkingSlot)->setTakenBy(this->id);
							StationEquipment::getEmergencyParkQueue().at(this->posInQ)->setAvailable(true);
							this->x = StationEquipment::getEmergencyParking().at(this->parkingSlot)->getX();
							this->y = StationEquipment::getEmergencyParking().at(this->parkingSlot)->getY();
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is parked at EMERGENCY parking space nr "+std::to_string(this->parkingSlot);
							
							std::this_thread::sleep_for(std::chrono::milliseconds(500));
							this->status = ClientStatus::wQStoreIn;
						} else {
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting for free EMERGENCY parking space";
						}
					}
					else 
					{
						if (lookForParking())
						{
							StationEquipment::getParking().at(this->parkingSlot)->setTakenBy(this->id);
							StationEquipment::getParkQueue().at(this->posInQ)->setAvailable(true);
							this->x = StationEquipment::getParking().at(this->parkingSlot)->getX();
							this->y = StationEquipment::getParking().at(this->parkingSlot)->getY();
							this->info = "Car "+std::to_string(this->id)+" is parked at parking space nr "+std::to_string(this->parkingSlot);
							
							std::this_thread::sleep_for(std::chrono::milliseconds(500));
							this->status = ClientStatus::wQStoreIn;
						} else {
							this->info = "Car "+std::to_string(this->id)+" is waiting for free parking space";
						}
					}
				}
				else if(this->status == ClientStatus::wQStoreIn)
				{
					if (this->emergency)
					{
						if(StationEquipment::getStoreEmergencyQueueIN().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to store";
							this->x = StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->getY();
							this->status = ClientStatus::qStoreIn;
						} else {
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting to go to the store";
						}
					}
					else 
					{
						if(StationEquipment::getStoreQueueIN().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							this->posInQ = StationEquipment::getQSize()-1;
							this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to store";
							this->x = StationEquipment::getStoreQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreQueueIN().at(this->posInQ)->getY();
							this->status = ClientStatus::qStoreIn;
						} else {
							this->info = "Car "+std::to_string(this->id)+" is waiting to go to the store";
						}
					}
				}
				else if(this->status == ClientStatus::qStoreIn)
				{
					if (this->emergency)
					{
						if(StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wDoorIn;
							}
							else
							{ 
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to store";
							}
						}
					}
					else
					{
						if(StationEquipment::getStoreQueueIN().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getStoreQueueIN().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getStoreQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreQueueIN().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wDoorIn;
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to store";
							}
						}
					}
				}
				else if(this->status == ClientStatus::wDoorIn)
				{
					if (this->emergency) StationEquipment::setEmergencyDoor(true);
					if(StationEquipment::getEmergencyDoor() && !this->emergency){
						this->info = "Car "+std::to_string(this->id)+" is WAITING to go IN while EMERGENCY";
					} else 
					{
						if (this->emergency)
						{
							if (StationEquipment::getCashEmergencyQueueIN().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								if(StationEquipment::getStationDoor().goIfAvailable())
								{
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is GOING IN through door!!!!!!!!!!!!!!!";
									sleepForTime(10);
									StationEquipment::setEmergencyDoor(false);
									StationEquipment::getStationDoor().setAvailable(true);
									StationEquipment::getStoreEmergencyQueueIN().at(this->posInQ)->setAvailable(true);
									this->posInQ = StationEquipment::getQSize()-1;
									this->x = StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->getX();
									this->y = StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->getY();
									this->progress = 0;
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in EE queue to cash desks$$$$$$$$$$$";
									this->status = ClientStatus::qCashDesk;
								} else {
									StationEquipment::getCashEmergencyQueueIN().at(StationEquipment::getQSize()-1)->setAvailable(true);
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to go IN";
								}
							} else {
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to go IN";
							}
						}
						else 
						{
							if (StationEquipment::getCashQueueIN().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								if(StationEquipment::getStationDoor().goIfAvailable())
								{
									this->info = "Car "+std::to_string(this->id)+" is GOING IN through door!!!!!!!!!!!!!!!";
									sleepForTime(10);
									StationEquipment::getStationDoor().setAvailable(true);
									StationEquipment::getStoreQueueIN().at(this->posInQ)->setAvailable(true);
									this->posInQ = StationEquipment::getQSize()-1;
									this->x = StationEquipment::getCashQueueIN().at(this->posInQ)->getX();
									this->y = StationEquipment::getCashQueueIN().at(this->posInQ)->getY();
									this->progress = 0;
									this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to cash desks$$$$$$$$$$$";
									this->status = ClientStatus::qCashDesk;
								} else {
									StationEquipment::getCashQueueIN().at(StationEquipment::getQSize()-1)->setAvailable(true);
									this->info = "Car "+std::to_string(this->id)+" is WAITING to go IN";
								}
							} else {
								this->info = "Car "+std::to_string(this->id)+" is WAITING to go IN";
							}
						}
					}
				}
				else if(this->status == ClientStatus::qCashDesk)
				{
					if (this->emergency)
					{
						if(StationEquipment::getCashEmergencyQueueIN().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->getY();
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wCashDesk;
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING for a free cash desk";
							}
							else
							{
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in EE queue to cash desks$$$$$$$$$$$";
							}
						}
					}
					else
					{
						if(StationEquipment::getCashQueueIN().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getCashQueueIN().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getCashQueueIN().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashQueueIN().at(this->posInQ)->getY();
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wCashDesk;
								this->info = "Car "+std::to_string(this->id)+" is WAITING for a free cash desk";
							}
							else
							{
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to cash desks$$$$$$$$$$$";
							}
						}
					}
				}
				else if(this->status == ClientStatus::wCashDesk)
				{
					if (this->emergency) StationEquipment::setEmergencyPay(true);
					if(lookForCashDesk())
					{
						this->info = "Car "+std::to_string(this->id)+" is PAYING at cash desk "+std::to_string(this->usedCashDesk)+"?????????????????????";
						this->x = StationEquipment::getCashDesks().at(this->usedCashDesk)->getX() + 3;
						this->y = StationEquipment::getCashDesks().at(this->usedCashDesk)->getY();
						if (this->emergency) {
							StationEquipment::getCashEmergencyQueueIN().at(this->posInQ)->setAvailable(true);
							StationEquipment::setEmergencyPay(false);
						}
						else StationEquipment::getCashQueueIN().at(this->posInQ)->setAvailable(true);
						sleepForTime(30);
						this->status = ClientStatus::wQCashOut;
					}
					else
					{
						this->info = "Car "+std::to_string(this->id)+" is WAITING for a free cash desk";
					}
				}
				else if (this->status == wQCashOut)
				{
					if (this->emergency)
					{
						if (StationEquipment::getCashEmergencyQueueOUT().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							StationEquipment::getCashDesks().at(this->usedCashDesk)->setAvailable(true);
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->getY();
							this->progress = 0;
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in EE queue to go OUT";
							this->status = ClientStatus::qCashOut;
						}
						else
						{
							this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to step out from cash desk";
						}
					}
					else
					{
						if (StationEquipment::getCashQueueOUT().at(StationEquipment::getQSize()-1)->takeIfAvailable())
						{
							StationEquipment::getCashDesks().at(this->usedCashDesk)->setAvailable(true);
							this->posInQ = StationEquipment::getQSize()-1;
							this->x = StationEquipment::getCashQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashQueueOUT().at(this->posInQ)->getY();
							this->progress = 0;
							this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to go OUT";
							this->status = ClientStatus::qCashOut;
						}
						else
						{
							this->info = "Car "+std::to_string(this->id)+" is WAITING to step out from cash desk";
						}
					}
				}
				else if(this->status == ClientStatus::qCashOut)
				{
					if (this->emergency)
					{
						if(StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->getY();
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wDoorOut;
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to go OUT";
							}
							else
							{
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in EE queue to go OUT";
							}
						}
					}
					else
					{
						if(StationEquipment::getCashQueueOUT().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getCashQueueOUT().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getCashQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getCashQueueOUT().at(this->posInQ)->getY();
							if (this->posInQ == 0)
							{
								this->status = ClientStatus::wDoorOut;
								this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT";
							}
							else
							{
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to go OUT";
							}
						}
					}
				}
				else if(this->status == ClientStatus::wDoorOut)
				{
					/*if (this->emergency) StationEquipment::setEmergencyDoor(true);
					if(StationEquipment::getEmergencyDoor() && !this->emergency){
						this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT while EMERGENCY";
					} else {
						if(StationEquipment::getStationDoor().goIfAvailable())
						{
							this->info = "Car "+std::to_string(this->id)+" is GOING OUT through door!!!!!!!!!!!!!!";
							sleepForTime(10);
							if (this->emergency) StationEquipment::setEmergencyDoor(false);
							StationEquipment::getStationDoor().setAvailable(true);
							
							if (this->emergency) StationEquipment::getEmergencyParking().at(this->parkingSlot)->setAvailable(true);
							else StationEquipment::getParking().at(this->parkingSlot)->setAvailable(true);
							
							this->status = ClientStatus::Driving;
						} else {
							this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT";
						}
					}*/
					if (this->emergency) StationEquipment::setEmergencyDoor(true);
					if(StationEquipment::getEmergencyDoor() && !this->emergency){
						this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT while EMERGENCY";
					} else 
					{
						if (this->emergency)
						{
							if (StationEquipment::getStoreEmergencyQueueOUT().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								if(StationEquipment::getStationDoor().goIfAvailable())
								{
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is GOING OUT through door!!!!!!!!!!!!!!!";
									sleepForTime(10);
									StationEquipment::setEmergencyDoor(false);
									StationEquipment::getStationDoor().setAvailable(true);
									StationEquipment::getCashEmergencyQueueOUT().at(this->posInQ)->setAvailable(true);
									this->posInQ = StationEquipment::getQSize()-1;
									this->x = StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->getX();
									this->y = StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->getY();
									this->progress = 0;
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in EE queue to parking";
									this->status = ClientStatus::qStoreOut;
								} else {
									StationEquipment::getStoreEmergencyQueueOUT().at(StationEquipment::getQSize()-1)->setAvailable(true);
									this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to go OUT";
								}
							} else {
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is WAITING to go OUT";
							}
						}
						else 
						{
							if (StationEquipment::getStoreQueueOUT().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								if(StationEquipment::getStationDoor().goIfAvailable())
								{
									this->info = "Car "+std::to_string(this->id)+" is GOING OUT through door!!!!!!!!!!!!!!!";
									sleepForTime(10);
									StationEquipment::getStationDoor().setAvailable(true);
									StationEquipment::getCashQueueOUT().at(this->posInQ)->setAvailable(true);
									this->posInQ = StationEquipment::getQSize()-1;
									this->x = StationEquipment::getStoreQueueOUT().at(this->posInQ)->getX();
									this->y = StationEquipment::getStoreQueueOUT().at(this->posInQ)->getY();
									this->progress = 0;
									this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to parking";
									this->status = ClientStatus::qStoreOut;
								} else {
									StationEquipment::getStoreQueueOUT().at(StationEquipment::getQSize()-1)->setAvailable(true);
									this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT";
								}
							} else {
								this->info = "Car "+std::to_string(this->id)+" is WAITING to go OUT";
							}
						}
					}
				}
				else if (this->status == ClientStatus::qStoreOut)
				{
					if (this->emergency)
					{
						if(StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								StationEquipment::getStoreEmergencyQueueOUT().at(this->posInQ)->setAvailable(true);
								this->status = ClientStatus::wOut;
							}
							else
							{ 
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to parking";
							}
						}
					}
					else
					{
						if(StationEquipment::getStoreQueueOUT().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getStoreQueueOUT().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getStoreQueueOUT().at(this->posInQ)->getX();
							this->y = StationEquipment::getStoreQueueOUT().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								StationEquipment::getStoreQueueOUT().at(this->posInQ)->setAvailable(true);
								this->status = ClientStatus::wOut;
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to parking";
							}
						}
					}
				}
				else if (this->status == ClientStatus::wOut)
				{
					if (this->emergency)
					{
						this->x = StationEquipment::getEmergencyParking().at(this->parkingSlot)->getX();
						this->y = StationEquipment::getEmergencyParking().at(this->parkingSlot)->getY();

						if(StationEquipment::getEmergencyOutQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(500));
								StationEquipment::getEmergencyParking().at(this->parkingSlot)->setAvailable(true);
								this->posInQ = StationEquipment::getQSize()-1;
								this->x = StationEquipment::getEmergencyOutQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getEmergencyOutQueue().at(this->posInQ)->getY();
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to drive OUT from STATION";
								this->status = ClientStatus::qOut;
							}
						else {
						this->info = "EMERGENCY Car "+std::to_string(this->id)+" is waiting to get out from EMERGENCY parking";
						}
					}
					else 
					{
						this->x = StationEquipment::getParking().at(this->parkingSlot)->getX();
						this->y = StationEquipment::getParking().at(this->parkingSlot)->getY();

						if(StationEquipment::getOutQueue().at(StationEquipment::getQSize()-1)->takeIfAvailable())
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(500));
								StationEquipment::getParking().at(this->parkingSlot)->setAvailable(true);
								this->posInQ = StationEquipment::getQSize()-1;
								this->x = StationEquipment::getOutQueue().at(this->posInQ)->getX();
								this->y = StationEquipment::getOutQueue().at(this->posInQ)->getY();
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to drive OUT from STATION";
								this->status = ClientStatus::qOut;
							}
						else {
						this->info = "Car "+std::to_string(this->id)+" is waiting to get out from parking";
						}
					}
				}
				else if (this->status == ClientStatus::qOut)
				{
					if (this->emergency)
					{
						if(StationEquipment::getEmergencyOutQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getEmergencyOutQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getEmergencyOutQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getEmergencyOutQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(500));
								StationEquipment::getEmergencyOutQueue().at(this->posInQ)->setAvailable(true);
								this->x = 0;
								this->y = 0;
								this->status = ClientStatus::Driving;
							}
							else
							{ 
								this->info = "EMERGENCY Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in emergency queue to drive OUT from STATION";
							}
						}
					}
					else
					{
						if(StationEquipment::getOutQueue().at(this->posInQ-1)->takeIfAvailable())
						{
							StationEquipment::getOutQueue().at(this->posInQ)->setAvailable(true);
							this->posInQ -= 1;
							this->x = StationEquipment::getOutQueue().at(this->posInQ)->getX();
							this->y = StationEquipment::getOutQueue().at(this->posInQ)->getY();
							
							if (this->posInQ == 0)
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(500));
								StationEquipment::getOutQueue().at(this->posInQ)->setAvailable(true);
								this->x = 0;
								this->y = 0;
								this->status = ClientStatus::Driving;
							}
							else
							{ 
								this->info = "Car "+std::to_string(this->id)+" is "+std::to_string(this->posInQ)+" in queue to drive OUT from STATION";
							}
						}
					}
				}
				else if (this->status == ClientStatus::Driving)
				{
					this->info = "Car "+std::to_string(this->id)+" is driving somewhere";
					sleepForTime( 2 * ((rand()%20)+10) );
					this->amountToTank = rand()%((9*this->capacity) / 10) + (this->capacity)/10;;
					this->status = ClientStatus::wStation;
					this->progress = 0;
				}
			}
		}
		
	private:
		int id;
		int x;
		int y;
		std::string model = "";
		Fuel type;
		int amountToTank;
		int capacity;
		bool emergency;
		
		ClientStatus status;
		std::string info = "";
		int progress = 0;
		int count;
		
		int posInQ = -1;
		int usedDistributor;
		int usedCashDesk;
		int parkingSlot;
		std::atomic_bool isDriving;
		std::unique_ptr<std::thread> lifeThread;

};


void drawMap()
{
	std::string empty = " ";
	std::string hline = "-";
	std::string hline2= "=";
	std::string twodot= ":";
	std::string vline = "|";
	std::string hasht = "#";
	
	std::string cpn1 = " __  __       ";
	std::string cpn2 = "/    |_| |\\  |";
	std::string cpn3 = "|    |   | \\ |";
	std::string cpn4 = "\\__  |   |  \\|";

	for (int y = 0; y<50; y++) // rysowanie ogólnej mapy stacji
		{
			for (int x = 0; x < 190; x++)
			{
				if ((y<3 && (x>141||x<128) ) || (y<2 && (x>140||x<129) ) || y>46 || x>186){
					attron(COLOR_PAIR(2)); 
					mvprintw(y,x,empty.c_str());
				}
				else if ((x<30 && (y<8 || (y>13&&y<17) || y>33)) || (x<3 && (y>7&&y<14))){
					attron(COLOR_PAIR(2));
					mvprintw(y,x,empty.c_str());
				}
				else{
					attron(COLOR_PAIR(1));
					if (x<30 && (y==17 || y==21 || y==25 || y==29 || y==33)) mvprintw(y,x,hline.c_str()); // linie kolejki do dystrybutora
					else if ( (x>39 && x<73) && (y>5&&y<43) ) // plac dystrybutorów
					{
						if (x==40 || x==72) mvprintw(y,x,twodot.c_str());
						else if (y==6 || y==42) mvprintw(y,x,hline.c_str());
					}
					else if ( (x<185&&x>169) && (y>5&&y<29) ) // parkingi
					{
						if( (x==184 || x==170) && y%2==0 ) mvprintw(y,x,twodot.c_str());
						else if( (x==181 || x==173) && y%2==1 ) mvprintw(y,x,"X");
						else if (x==177) mvprintw(y,x,twodot.c_str());
						else if (y%2==0) mvprintw(y,x,hline.c_str());
					}
					else if ( (x<166&&x>127) && (y>2 && y<9) ) // wyjazd ze stacji
					{
						if ( (x==142&&y<5) || (x==128&&y<8) ) mvprintw(y,x,twodot.c_str());
						else if ( (y==4&&x>142) || (y==8&&x<160) ) mvprintw(y,x,hline.c_str());
					}
					else if ( (x>115&x<135) && (y<32&&y>27) ){ // napis CPN
						if (x==118){
							if (y==28) mvprintw(y,x,cpn1.c_str());
							else if (y==29) mvprintw(y,x,cpn2.c_str());
							else if (y==30) mvprintw(y,x,cpn3.c_str());
							else if (y==31) mvprintw(y,x,cpn4.c_str());
						}
					}
					else if ( (x>100&&x<152) && (y<45 && y>31) ) // sklep
					{
						if (x==101 || x==151){
							if (x==151)
							{
								if (StationEquipment::getStationDoor().getAvailable())
								{
									if (y==37 || y==38 || y==39) mvprintw(y,x,twodot.c_str());
									else mvprintw(y,x,vline.c_str());
								} else {
									if (y==37 || y==39) mvprintw(y,x,hline2.c_str());
									else if (y==38) mvprintw(y,x,empty.c_str());
									else mvprintw(y,x,vline.c_str());
								}
							}
							else mvprintw(y,x,vline.c_str());
						}
						else if (y==44 || y==32) mvprintw(y,x,hline2.c_str());
						else if ( (x<146&&x>117) && (y<44&y>32) ) // kolejki w sklepie 
						{
							if (y==33 || y==36 || y==40 || y==43 ) mvprintw(y,x,hline.c_str());
							else if ( (y>36&&y<40) && (x%5<3) ) mvprintw(y,x,hasht.c_str());
						}
						else if (x==113) // linia kas
						{
							mvprintw(y,x,twodot.c_str());
						}
					}
					else if ( (x>86&&x<150) && (y==17 || y==27) ) //przejazd od dystrybutorów na parking
					{
						mvprintw(y,x,hline.c_str());
					}
					else if ((x==153&&(y>29&&y<37)) ||(x==174&&(y>32&&y<44))) // przejście pomiędzy parkingiem a sklepem
					{
						mvprintw(y,x,twodot.c_str());
					}
					
				}
			}
		}	
}
class Station{
	public:
		Station(){
			this->stationRunning = true;
		}

		
		void initStation(int amClients, int amEmerg, int amNormal, int amElectric, int queueLength, int parkingSize, int amCashiers, int amCashDesks)
		{
			if (amNormal<1) amNormal = 1;
			if (amNormal>5) amNormal = 5;
			if (amElectric<1) amElectric = 1;
			if ((amElectric+2*amNormal)>11) amElectric = 1;
			if (amCashDesks>3) amCashDesks = 3;
			if (amCashDesks<1) amCashDesks = 1;
			if (amEmerg<0) amEmerg = 0;
			if (parkingSize<1) parkingSize = 1;
			if (parkingSize>19) parkingSize = 19;
			if (amCashiers<1) amCashiers = 1;

			for (int i=0;i<amClients/3;i++)
			{
				this->clients.push_back(new Client(i,Fuel::petrol,50,false));
			}
			for (int i=amClients/3;i<2*(amClients/3);i++)
			{
				this->clients.push_back(new Client(i,Fuel::diesel,40,false));
			}
			for (int i=2*(amClients/3);i<amClients;i++)
			{
				this->clients.push_back(new Client(i,Fuel::electric,100,false));
			}
			for (int i=amClients;i<(amClients+amEmerg);i++)
			{
				int r = rand()%3;
				Fuel f;
				if (r == 0) f = Fuel::petrol;
				else if (r == 1) f = Fuel::diesel;
				else f = Fuel::electric;

				this->clients.push_back(new Client(i,f,70,true));
			}
			for (int i=0;i<2*amNormal;i+=2)
			{
				StationEquipment::getDistributors().push_back(new Distributor(i, Fuel::petrol, 300, 53, (9+3*i)));
				StationEquipment::getDistributors().push_back(new Distributor(i+1, Fuel::diesel, 300, 53, (11+3*i)));
			}
			
			for (int i=0;i<amElectric;i++)
			{
				int p = 9;
				if (i%2==1) p = 8;
				StationEquipment::getDistributors().push_back(new Distributor((2*amNormal)+i, Fuel::electric, 200, 53, (p+3*((2*amNormal)+i))));
			}
			
			this->tanks.push_back(new Tank(0, Fuel::petrol,15,9));
			this->tanks.push_back(new Tank(1, Fuel::diesel,15,11));
			
			for (int i=0;i<queueLength;i++)
			{
				StationEquipment::getPetrolQueue().push_back(new CarSlot(i,(30-6*i),19));
				StationEquipment::getDieselQueue().push_back(new CarSlot(i,(30-6*i),23));
				StationEquipment::getElectricQueue().push_back(new CarSlot(i,(30-6*i),27));
				StationEquipment::getEmergencyQueue().push_back(new CarSlot(i,(30-6*i),31));
				StationEquipment::getParkQueue().push_back(new CarSlot(i, (148-10*i), 20));
				StationEquipment::getEmergencyParkQueue().push_back(new CarSlot(i, (148-10*i), 24));
				StationEquipment::getStoreQueueIN().push_back(new CarSlot(i,154,(36-i)));
				StationEquipment::getStoreQueueOUT().push_back(new CarSlot(i,159,(31+i)));
				StationEquipment::getStoreEmergencyQueueIN().push_back(new CarSlot(i,164,(36-i)));
				StationEquipment::getStoreEmergencyQueueOUT().push_back(new CarSlot(i,169,(31+i)));
				StationEquipment::getCashQueueIN().push_back(new CarSlot(i,(121+5*i),34));
				StationEquipment::getCashQueueOUT().push_back(new CarSlot(i,(143-5*i),42));
				StationEquipment::getCashEmergencyQueueIN().push_back(new CarSlot(i,(121+5*i),35));
				StationEquipment::getCashEmergencyQueueOUT().push_back(new CarSlot(i,(143-5*i),41));
				StationEquipment::getOutQueue().push_back(new CarSlot(i,(140+5*i),5));
				StationEquipment::getEmergencyOutQueue().push_back(new CarSlot(i,(140+5*i),7));
			}
			StationEquipment::setQSize(queueLength);
			
			for (int i=0;i<parkingSize;i++)
			{
				int x;
				if (i%2==0) x = 172;
				else x = 179;
				StationEquipment::getParking().push_back(new CarSlot(i,x,(7 + (i/2)*2)));
			}
			/*for (int i=0;i<3;i++)
			{
				StationEquipment::getEmergencyParking().push_back(new CarSlot(i));
			}*/
			StationEquipment::getEmergencyParking().push_back(new CarSlot(0, 172, 27));
			StationEquipment::getEmergencyParking().push_back(new CarSlot(1, 179, 27));
			StationEquipment::getEmergencyParking().push_back(new CarSlot(2, 179, 25));
			for (int i=0;i<amCashiers;i++)
			{
				this->cashiers.push_back(new Cashier(i));
			}
			for (int i=0;i<amCashDesks;i++)
			{
				StationEquipment::getCashDesks().push_back(new CashDesk(i, 110, (34+4*i)));
			}
		}
		
		void runSimulation(){
			for (Client* c : this->clients)
			{
				c->start();
			}
			for (Tank* t : this->tanks)
			{
				t->start();
			}
			for (Cashier* c : this->cashiers)
			{
				c->start();
			}
			showStatusThread = std::make_unique<std::thread>([this]() 
			{
				while (this->stationRunning)
				{
					
					clear();
					drawMap();

					attron(COLOR_PAIR(1));
					for (CarSlot* s : StationEquipment::getParking())
					{
						mvprintw(s->getY(), s->getX(), s->getTakenBy().c_str());
					}
					for (CarSlot* s : StationEquipment::getEmergencyParking())
					{
						mvprintw(s->getY(), s->getX(), s->getTakenBy().c_str());
					}

					for (Client* c : this->clients)
					{
						if(c->getType()==Fuel::petrol) attron(COLOR_PAIR(3));
						else if (c->getType()==Fuel::diesel) attron(COLOR_PAIR(4));
						else attron(COLOR_PAIR(5));
						mvprintw(c->getY(), c->getX(), c->getModel().c_str());
						if (c->getProgress()!=0){
							std::string str = std::to_string(c->getProgress())+"%%";
							mvprintw(c->getY(), c->getX()+4, str.c_str());
						}
					}
					
					for (Tank* t : this->tanks)
					{
						if(t->getType()==Fuel::petrol) attron(COLOR_PAIR(3));
						else attron(COLOR_PAIR(4));
						mvprintw(t->getY(), t->getX(), t->getModel().c_str());
						if (t->getProgress()!=0){
							std::string str = std::to_string(t->getProgress())+"%%";
							mvprintw(t->getY(), t->getX()+6, str.c_str());
						}
					}
					
					attron(COLOR_PAIR(1));
					for (Distributor* d : StationEquipment::getDistributors())
					{
						mvprintw(d->getY(), d->getX()-3, d->getModel().c_str());
					}

					std::string x2hline = "||";

					for (CashDesk* cd : StationEquipment::getCashDesks())
					{
						if (cd->getInUse()) attron(COLOR_PAIR(6));
						else attron(COLOR_PAIR(1));
						mvprintw(cd->getY()+1, cd->getX(), x2hline.c_str());
						mvprintw(cd->getY(), cd->getX(), cd->getModel().c_str());
						mvprintw(cd->getY()-1, cd->getX(), x2hline.c_str());
					}

					int money = 0;
					for (Cashier* c : this->cashiers)
					{
						attron(COLOR_PAIR(6));	
						mvprintw(c->getY(), c->getX(), c->getModel().c_str());
						if (c->getProgress()!=0){
							std::string str = std::to_string(c->getProgress())+"%%";
							mvprintw(c->getY(), c->getX()-4, str.c_str());
						}
						money += c->getMoneyMade();
					}
					attron(COLOR_PAIR(7));
					std::string str1 = "Przychód stacji :";
					std::string str2 = std::to_string(money)+"$";
					mvprintw(0,0, str1.c_str());
					mvprintw(1,3, str2.c_str());

					attron(COLOR_PAIR(3));
					mvprintw(49,0,"[] - petrol ");
					attron(COLOR_PAIR(4));
					mvprintw(49,12,"[] - diesel ");
					attron(COLOR_PAIR(5));
					mvprintw(49,24,"[] - electric ");
					attron(COLOR_PAIR(1));
					mvprintw(49,38,"<> - emergency ");
					attron(COLOR_PAIR(1));
					mvprintw(49,53,"() - empty car ");
					attron(COLOR_PAIR(6));
					mvprintw(49,68,"@ - cashier ");
					attron(COLOR_PAIR(1));
					mvprintw(49,90," Press SPACE to end the simulation ");
					
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			});
		}
		
		void endSimulation()
		{
			for (Cashier* c : this->cashiers)
			{
				c->stop();
			}

			for (Tank* t : this->tanks)
			{
				cvP.notify_all();
				cvD.notify_all();
				t->stop();
			}			

			for (Client* c : this->clients)
			{
				c->stop();
			}
			
			this->stationRunning = false;
			showStatusThread->join();
			
			clear();
			mvprintw(0,0, "Times each car has refueled :");
			int rowCnt = 1;
			for (Client* c : this->clients)
			{
				if(c->getType()==Fuel::petrol) attron(COLOR_PAIR(3));
				else if (c->getType()==Fuel::diesel) attron(COLOR_PAIR(4));
				else attron(COLOR_PAIR(5));

				mvprintw(rowCnt, 0, "ID : ");
				mvprintw(rowCnt, 5, std::to_string(c->getId()).c_str());
				mvprintw(rowCnt, 8, " - ");
				mvprintw(rowCnt, 11, std::to_string(c->getCount()).c_str());
				if (c->getEmerg()){ 
					attron(COLOR_PAIR(1));
					printw(" <- emergency client");
				}				
				
				delete c;
				rowCnt++;
			}
			rowCnt++;
			mvprintw(rowCnt, 2, "Press SPACE to exit");
			for (Cashier* c : this->cashiers)
			{
				delete c;
			}
			for (Distributor* d : StationEquipment::getDistributors())
			{
				delete d;
			}
			for (Tank* t : this->tanks)
			{
				delete t;
			}
			for (CarSlot* s : StationEquipment::getPetrolQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getDieselQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getElectricQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getEmergencyQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getParkQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getEmergencyParkQueue())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getParking())
			{
				delete s;
			}
			for (CarSlot* s : StationEquipment::getEmergencyParking())
			{
				delete s;
			}
			for (CashDesk* cd : StationEquipment::getCashDesks())
			{
				delete cd;
			}
			
			
		}
	private:
		std::atomic_bool stationRunning;
		std::vector<Client*> clients;
		std::vector<Tank*> tanks;
		std::vector<Cashier*> cashiers;
		std::unique_ptr<std::thread> showStatusThread;
};


///////////
int main(int argc, char** argv) {
	srand(time(NULL));
	
	initscr();
	int rows = 50;
	int columns = 190;
	wresize(stdscr, rows, columns);
	if (has_colors() == true)
	{
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_WHITE, COLOR_GREEN);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_YELLOW, COLOR_BLACK);
		init_pair(5, COLOR_BLUE, COLOR_BLACK);
		init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(7, COLOR_YELLOW, COLOR_GREEN);
		
	}
	attron(COLOR_PAIR(1));
	int cars, emerg, nDist, eDist, park, desks, cash;

	mvprintw(0,0,"Enter the number of regular cars : ");
	scanw("%d", &cars);

	mvprintw(1,0,"Enter the number of emergency cars : ");
	scanw("%d", &emerg);
	
	mvprintw(3,0,"There can be up to 11 distributors (normal + electric).");
	mvprintw(4,0,"Enter the number of normal distributors (");
	attron(COLOR_PAIR(3)); mvprintw(4,41,"petrol");
	attron(COLOR_PAIR(1)); mvprintw(4,47," and ");
	attron(COLOR_PAIR(4)); mvprintw(4,52,"diesel");
	attron(COLOR_PAIR(1)); mvprintw(4,58,") (1-5) : ");
	scanw("%d", &nDist);

	mvprintw(6,0,"Enter the number of ");
	attron(COLOR_PAIR(5)); mvprintw(6,20,"electric");
	attron(COLOR_PAIR(1)); mvprintw(6,28," car-chargers : ");
	scanw("%d", &eDist);

	mvprintw(8,0,"Enter the number of parking spaces (1-19) : ");
	scanw("%d", &park);

	mvprintw(10,0,"Enter the number of cash desks (1-3) : ");
	scanw("%d", &desks);

	mvprintw(12,0,"Enter the number of working ");
	attron(COLOR_PAIR(6)); mvprintw(12,28,"cashiers");
	attron(COLOR_PAIR(1)); mvprintw(12,36,"(at least one) : ");
	scanw("%d", &cash);
	
	cbreak();
	timeout(10);
	noecho();
	Station station;
	station.initStation(cars,emerg,nDist,eDist,6,park,cash,desks); //klienci, normalne, elektryczne, kolejka, parking, kasjerzy, kasy
	
	station.runSimulation();
	while(getch() != 32)
	{
	}
	station.endSimulation();
	while(getch() != 32)
	{
	}
	if (has_colors()==true)
		attroff(COLOR_PAIR(1));
	endwin();
	
	return 0;
}